"""Landmarks y esqueleto 2D a partir de la silueta (README §5), sin IA.

Vista frontal, personaje de pie en pose A con los brazos despegados:

1. Perfil de anchura por filas (tramos de máscara por fila): cabeza, cuello, hombros, axilas
   y entrepierna.
2. Seguimiento de tramos fila a fila para brazos y piernas. El grosor local sale de la
   transformada de distancia (2·DT sobre el eje).
3. Codos, muñecas, rodillas y tobillos: mínimos locales de grosor cerca de la proporción
   antropométrica esperada.
4. Cara: proporciones canónicas, refinadas con los dos blobs oscuros más simétricos (ojos) y el
   máximo de R−G (boca).

Lo que no se detecta cae en proporciones por defecto, con confianza baja y un aviso. Todo se
puede corregir arrastrando en la UI.
"""
from __future__ import annotations

from dataclasses import dataclass, field

import cv2
import numpy as np
from scipy.ndimage import minimum_filter1d, uniform_filter1d

from .template.landmarks2d import LANDMARKS_2D, LABELS_ES, derived_points

# Proporciones por defecto (fracciones de la altura desde la coronilla; x desde el eje, +x = izquierda
# del personaje = derecha de la imagen). Medidas sobre la plantilla en pose A.
DEFAULT_LAYOUT = {
    "crown": (0.0, 0.0), "chin": (0.0, 0.127), "neck": (0.0, 0.155), "crotch": (0.0, 0.52),
    "eye_l": (0.021, 0.069), "eye_r": (-0.021, 0.069), "nose": (0.0, 0.088), "mouth": (0.0, 0.11),
    "shoulder_l": (0.13, 0.22), "elbow_l": (0.24, 0.33), "wrist_l": (0.34, 0.43), "fingertip_l": (0.43, 0.52),
    "hip_l": (0.055, 0.53), "knee_l": (0.055, 0.71), "ankle_l": (0.05, 0.92),
}
for _k in list(DEFAULT_LAYOUT):
    if _k.endswith("_l"):
        x, y = DEFAULT_LAYOUT[_k]
        DEFAULT_LAYOUT[_k[:-2] + "_r"] = (-x, y)

# Antropometría (fracción de la longitud hombro→punta de dedos / cadera→tobillo).
ELBOW_FRAC, WRIST_FRAC, KNEE_FRAC = 0.36, 0.70, 0.45  # la muñeca se busca en ±0,1


@dataclass
class Landmarks2D:
    points: dict                       # nombre -> (x, y) en píxeles de la imagen de trabajo
    confidence: dict                   # nombre -> 0..1 (1 detectado, < 0.5 proporción por defecto)
    auto: dict = field(default_factory=dict)   # resultado automático (para "restablecer")
    manual: set = field(default_factory=set)   # puntos movidos a mano
    top: float = 0.0                   # fila de la coronilla
    bottom: float = 0.0                # fila de las plantas
    head_width: float = 0.0
    warnings: list = field(default_factory=list)

    @property
    def height_px(self) -> float:
        return self.bottom - self.top

    @property
    def head_height(self) -> float:
        return self.points["chin"][1] - self.top

    @property
    def heads(self) -> float:
        """Altura total en cabezas (7,5 realista; 5–6 estilizado)."""
        return self.height_px / max(self.head_height, 1e-6)

    def all_points(self) -> dict:
        return {**self.points, **derived_points(self.points)}

    def move(self, name: str, x: float, y: float) -> None:
        self.points[name] = (float(x), float(y))
        self.manual.add(name)

    def reset(self, name: str | None = None) -> None:
        for k in [name] if name else list(self.manual):
            self.points[k] = self.auto[k]
            self.manual.discard(k)

    def to_dict(self) -> dict:
        return {"points": {k: [round(float(v[0]), 2), round(float(v[1]), 2)] for k, v in self.points.items()},
                "confidence": {k: round(float(v), 2) for k, v in self.confidence.items()},
                "manual": sorted(self.manual), "top": float(self.top), "bottom": float(self.bottom),
                "head_width": float(self.head_width), "heads": round(self.heads, 2), "warnings": self.warnings}


def row_runs(mask: np.ndarray) -> list[np.ndarray]:
    """Tramos [inicio, fin) de máscara en cada fila."""
    m = (mask > 0).astype(np.int8)
    d = np.diff(np.pad(m, ((0, 0), (1, 1))), axis=1)
    rs, cs = np.nonzero(d == 1)
    _, ce = np.nonzero(d == -1)
    bounds = np.searchsorted(rs, np.arange(mask.shape[0] + 1))
    return [np.stack([cs[a:b], ce[a:b]], 1) for a, b in zip(bounds[:-1], bounds[1:])]


def _run_at(runs: np.ndarray, x: float):
    for s, e in runs:
        if s <= x < e:
            return int(s), int(e)
    return None


def _track(runs: list, y0: int, run0: tuple, y_end: int, max_growth: float = 2.2, avoid_x: float | None = None):
    """Sigue un tramo hacia abajo por solapamiento. Devuelve [(y, s, e)] y si acabó fusionándose."""
    out = [(y0, *run0)]
    prev = run0
    widths = [run0[1] - run0[0]]
    for y in range(y0 + 1, y_end + 1):
        cand = [(int(s), int(e)) for s, e in runs[y] if s < prev[1] and e > prev[0]]
        if not cand:
            return out, False
        r = max(cand, key=lambda r: min(r[1], prev[1]) - max(r[0], prev[0]))
        w = r[1] - r[0]
        if (avoid_x is not None and r[0] <= avoid_x < r[1]) or w > max_growth * np.median(widths[-30:]):
            return out, True
        out.append((y, *r))
        widths.append(w)
        prev = r
    return out, False


def _pick_min(s: np.ndarray, t: np.ndarray, target: float, window: float, L: float) -> int | None:
    """Mínimo local de grosor más cercano a la posición esperada.

    El grosor sólo desempata: los dedos vistos de canto son más finos que la muñeca.
    """
    if len(t) < 5:
        return None
    k = max(2, len(t) // 60)
    ts = uniform_filter1d(t.astype(float), 2 * k + 1, mode="nearest")
    local = ts <= minimum_filter1d(ts, 2 * max(3, len(t) // 25) + 1, mode="nearest") + 1e-6
    cand = local & (np.abs(s - target) <= window * L)
    if not cand.any():
        return None
    score = ts / np.median(ts) + 200.0 * ((s - target) / L) ** 2  # domina la cercanía al objetivo
    score[~cand] = np.inf
    return int(np.argmin(score))


def _taper_end(s: np.ndarray, t: np.ndarray, lo: float, hi: float, L: float) -> int | None:
    """Fin del estrechamiento (muñeca): primer punto de la ventana que alcanza el grosor mínimo.

    De canto, la mano es tan fina como la muñeca, así que el mínimo es una meseta; la muñeca
    está donde el antebrazo deja de adelgazar.
    """
    win = np.flatnonzero((s >= lo * L) & (s <= hi * L))
    if len(win) < 3:
        return None
    ts = uniform_filter1d(t.astype(float), 2 * max(2, len(t) // 60) + 1, mode="nearest")
    m = ts[win].min()
    return int(win[np.argmax(ts[win] <= m * 1.03)])


class _Detector:
    def __init__(self, mask: np.ndarray, rgb: np.ndarray | None):
        self.mask = mask
        self.rgb = rgb
        ys = np.flatnonzero(mask.any(1))
        if len(ys) < 50:
            raise ValueError("máscara vacía o demasiado pequeña")
        self.top, self.bottom = int(ys[0]), int(ys[-1])
        self.H = self.bottom - self.top
        self.runs = row_runs(mask)
        self.dt = cv2.distanceTransform(mask, cv2.DIST_L2, 5)
        self.pts: dict = {}
        self.conf: dict = {}
        self.warn: list = []

    def set(self, name, x, y, conf=1.0):
        self.pts[name] = (float(x), float(y))
        self.conf[name] = conf

    def y(self, frac: float) -> int:
        return int(np.clip(self.top + frac * self.H, self.top, self.bottom))

    # ------------------------------------------------------------ cabeza / tronco
    def head_and_neck(self):
        top_runs = self.runs[self.top]
        s, e = max(top_runs, key=lambda r: r[1] - r[0])
        self.cx = (s + e - 1) / 2
        self.set("crown", self.cx, self.top)
        rows = range(self.top, self.y(0.4))
        widths = np.array([(lambda r: 0 if r is None else r[1] - r[0])(_run_at(self.runs[y], self.cx)) for y in rows])
        y_hmax = int(np.argmax(widths[: max(2, int(0.16 * self.H))]))
        self.head_width = float(widths[y_hmax])
        running, shoulder = np.inf, None
        for i in range(y_hmax, len(widths)):
            running = min(running, widths[i])
            if widths[i] > 1.9 * running and i > y_hmax + 0.03 * self.H:
                shoulder = i
                break
        if shoulder is None:
            self.warn.append("No se encuentran el cuello y los hombros en el perfil de anchura.")
            return False
        seg = widths[y_hmax:shoulder]
        neck_w = seg.min()
        i0 = y_hmax + int(np.argmin(seg))
        a = b = i0
        while a > y_hmax and widths[a - 1] <= neck_w * 1.06:
            a -= 1
        while b < shoulder - 1 and widths[b + 1] <= neck_w * 1.06:
            b += 1
        chin = next(i for i in range(y_hmax, shoulder) if widths[i] <= neck_w * 1.3)
        yn = self.top + (a + b + 2 * shoulder) / 4
        r = _run_at(self.runs[int(yn)], self.cx)
        self.cx = (r[0] + r[1] - 1) / 2 if r else self.cx
        self.set("neck", self.cx, yn)
        self.set("chin", self.cx, self.top + chin)
        self.shoulder_top = self.top + shoulder
        self.neck_w = float(neck_w)
        return True

    def _split(self, y):
        rr = self.runs[y]
        t = _run_at(rr, self.cx)
        if t is None:
            return None
        left = [r for r in rr if r[1] <= t[0]]
        right = [r for r in rr if r[0] >= t[1]]
        return (t, left[-1], right[0]) if left and right else None

    def armpits(self):
        k = max(3, int(0.012 * self.H))  # el hueco de la axila debe mantenerse k filas seguidas
        for y in range(self.shoulder_top, self.y(0.6)):
            if all(self._split(y + i) for i in range(k)):
                self.armpit_y = y
                self.torso, l, r = self._split(y)
                self.arm_start = y + k
                _, l, r = self._split(self.arm_start)
                return (int(l[0]), int(l[1])), (int(r[0]), int(r[1]))
        self.warn.append("Brazos pegados al torso: usa pose A con los brazos despegados o corrige los puntos.")
        return None

    # ------------------------------------------------------------------ brazos
    def arm(self, side: str, run0):
        samples, merged = _track(self.runs, self.arm_start, run0, self.bottom, avoid_x=self.cx)
        if len(samples) < 0.05 * self.H:
            self.warn.append(f"No se pudo seguir el brazo {'izquierdo' if side == 'l' else 'derecho'}.")
            return
        if merged:
            self.warn.append(f"La mano {'izquierda' if side == 'l' else 'derecha'} toca el cuerpo; revisa muñeca y dedos.")
        S = np.array(samples, float)
        C = np.stack([(S[:, 1] + S[:, 2] - 1) / 2, S[:, 0]], 1)
        up = C[: max(3, int(len(C) * 0.6))]
        d = np.linalg.svd(up - up.mean(0))[2][0]
        if d[1] < 0:
            d = -d
        r_arm = float(np.median(self.dt[C[: max(3, len(C) // 3), 1].astype(int), C[: max(3, len(C) // 3), 0].astype(int)]))
        # Hombro: sobre el eje del brazo, subiendo desde la axila hasta quedar a un radio del brazo
        # y medio por debajo del contorno superior del hombro.
        h, w = self.mask.shape
        p = C[0].copy()
        for _ in range(int(0.3 * self.H)):
            x = int(np.clip(p[0], 0, w - 1))
            col = np.flatnonzero(self.mask[:, x])
            if not len(col) or p[1] - col[0] <= 1.5 * r_arm or not self.mask[int(p[1]), x]:
                break
            p -= d
        shoulder = p
        tail = S[-6:]
        ends = np.concatenate([np.stack([tail[:, 1], tail[:, 0]], 1), np.stack([tail[:, 2] - 1, tail[:, 0]], 1)])
        tip = ends[np.argmax((ends - shoulder) @ d)]
        L = float((tip - shoulder) @ d)
        s = (C - shoulder) @ d
        t = 2 * self.dt[C[:, 1].astype(int), C[:, 0].astype(int)]
        self.set(f"shoulder_{side}", *shoulder)
        self.set(f"fingertip_{side}", *tip, conf=0.7 if merged else 1.0)
        for name, frac in (("elbow", ELBOW_FRAC), ("wrist", WRIST_FRAC)):
            i = _pick_min(s, t, frac * L, 0.10, L) if name == "elbow" else _taper_end(s, t, frac - 0.1, frac + 0.1, L)
            if i is None:
                q = shoulder + d * frac * L
                self.set(f"{name}_{side}", *q, conf=0.6)
            else:
                self.set(f"{name}_{side}", *C[i], conf=1.0)

    # ------------------------------------------------------------------ piernas
    def legs(self):
        y = getattr(self, "armpit_y", self.y(0.3))
        while y < self.bottom and self.mask[y, int(self.cx)]:
            y += 1
        if y >= self.bottom - 0.2 * self.H:
            self.warn.append("No se ve la separación entre las piernas; caderas y rodillas son aproximadas.")
            return
        self.set("crotch", self.cx, y)
        yy = min(y + 2, self.bottom)
        rr = self.runs[yy]
        left = [r for r in rr if r[1] <= self.cx]
        right = [r for r in rr if r[0] > self.cx]
        for side, cand in (("r", left[-1:]), ("l", right[:1])):
            if not cand:
                continue
            samples, _ = _track(self.runs, yy, (int(cand[0][0]), int(cand[0][1])), self.bottom, max_growth=3.0)
            S = np.array(samples, float)
            C = np.stack([(S[:, 1] + S[:, 2] - 1) / 2, S[:, 0]], 1)
            t = 2 * self.dt[C[:, 1].astype(int), C[:, 0].astype(int)]
            hip = np.array([C[min(len(C) - 1, int(0.01 * self.H))][0], y + 0.012 * self.H])
            self.set(f"hip_{side}", *hip)
            ys = C[:, 1]
            ia = _pick_min(ys, t, self.bottom - 0.065 * self.H, 0.035, self.H)
            ankle = C[ia] if ia is not None else np.array([C[-1][0], self.bottom - 0.065 * self.H])
            self.set(f"ankle_{side}", *ankle, conf=1.0 if ia is not None else 0.6)
            L = float(ankle[1] - hip[1])
            ik = _pick_min(ys - hip[1], t, KNEE_FRAC * L, 0.08, L)
            knee = C[ik] if ik is not None else hip + (ankle - hip) * KNEE_FRAC
            self.set(f"knee_{side}", *knee, conf=1.0 if ik is not None else 0.6)

    # -------------------------------------------------------------------- cara
    def face(self):
        hh = self.pts["chin"][1] - self.top
        cx, w = self.cx, self.head_width
        eye_y, ex = self.top + 0.54 * hh, 0.19 * w
        self.set("eye_l", cx + ex, eye_y, 0.4)
        self.set("eye_r", cx - ex, eye_y, 0.4)
        self.set("nose", cx, self.top + 0.70 * hh, 0.4)
        self.set("mouth", cx, self.top + 0.86 * hh, 0.4)
        if self.rgb is None:
            return
        x0, x1 = int(cx - 0.4 * w), int(cx + 0.4 * w)
        y0, y1 = int(self.top + 0.35 * hh), int(self.top + 0.7 * hh)
        if x1 - x0 < 10 or y1 - y0 < 10:
            return
        gray = cv2.cvtColor(self.rgb[y0:y1, x0:x1], cv2.COLOR_RGB2GRAY)
        inside = self.mask[y0:y1, x0:x1] > 0
        thr = 0.6 * np.median(gray[inside]) if inside.any() else 0
        dark = ((gray <= thr) & inside).astype(np.uint8)
        n, _, stats, cent = cv2.connectedComponentsWithStats(dark, connectivity=8)
        bh, bw = dark.shape
        valid = set()
        for i in range(1, n):  # fuera el pelo: toca el borde de la ventana o es demasiado grande
            x, y, ww, hh, area = stats[i]
            if x > 0 and y > 0 and x + ww < bw and y + hh < bh and area < (0.18 * w) ** 2:
                valid.add(i)
        best = None
        for i in range(1, n):
            if i not in valid:
                continue
            for j in range(i + 1, n):
                if j not in valid:
                    continue
                (xi, yi), (xj, yj) = cent[i], cent[j]
                sep = abs(xi - xj)
                if not (0.2 * w <= sep <= 0.6 * w):
                    continue
                if min(stats[i, cv2.CC_STAT_AREA], stats[j, cv2.CC_STAT_AREA]) < 4:
                    continue
                cost = abs(yi - yj) + abs((xi + xj) / 2 + x0 - cx)
                if cost < 0.08 * w and (best is None or cost < best[0]):
                    best = (cost, cent[i] + (x0, y0), cent[j] + (x0, y0))
        if best:
            a, b = sorted(best[1:], key=lambda p: p[0])
            self.set("eye_r", *a)
            self.set("eye_l", *b)
            eye_y = (a[1] + b[1]) / 2
            self.set("nose", cx, eye_y + 0.35 * (self.pts["chin"][1] - eye_y), 0.6)
        my0, my1 = int(eye_y + 0.45 * (self.pts["chin"][1] - eye_y)), int(self.pts["chin"][1])
        band = self.rgb[my0:my1, int(cx - 0.15 * w):int(cx + 0.15 * w)].astype(np.int16)
        if band.size:
            rg = (band[..., 0] - band[..., 1]).mean(1)
            if rg.max() - np.median(rg) > 12:
                self.set("mouth", cx, my0 + int(np.argmax(rg)), 0.8)

    # --------------------------------------------------------------- respaldo
    def fill_defaults(self):
        H = self.H
        for name in LANDMARKS_2D:
            if name not in self.pts:
                dx, dy = DEFAULT_LAYOUT[name]
                self.set(name, self.cx + dx * H, self.top + dy * H, 0.2)


def detect_landmarks(mask: np.ndarray, rgb: np.ndarray | None = None) -> Landmarks2D:
    det = _Detector(mask, rgb)
    if det.head_and_neck():
        arms = det.armpits()
        if arms:
            det.arm("r", arms[0])   # la derecha del personaje sale a la izquierda de la imagen
            det.arm("l", arms[1])
        det.legs()
        det.face()
    else:
        det.cx = float(np.median(np.nonzero(mask)[1]))
        det.head_width = 0.1 * det.H
    det.fill_defaults()
    low = [LABELS_ES[k] for k, c in det.conf.items() if c < 0.5 and k not in ("eye_l", "eye_r", "nose", "mouth")]
    if low:
        det.warn.append("Puntos estimados por proporción (revisar): " + ", ".join(low) + ".")
    lm = Landmarks2D(points=det.pts, confidence=det.conf, top=det.top, bottom=det.bottom,
                     head_width=det.head_width, warnings=det.warn)
    lm.auto = dict(lm.points)
    if lm.heads < 6.0:
        lm.warnings.append(f"Proporción de {lm.heads:.1f} cabezas: personaje estilizado o foto tomada demasiado "
                           "cerca (perspectiva). Usa la plantilla estilizada o una foto a ≥ 3 m con zoom.")
    return lm
