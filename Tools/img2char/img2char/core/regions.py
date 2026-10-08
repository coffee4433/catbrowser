"""Partes 2D (watershed) y zonas de material (k-means + reglas) (README §4)."""
from __future__ import annotations

import cv2
import numpy as np
from scipy.ndimage import distance_transform_edt

from .landmarks import Landmarks2D
from .preprocess import smooth_for_segmentation

PARTS_2D = ["head", "torso", "arm_l", "arm_r", "hand_l", "hand_r", "leg_l", "leg_r", "foot_l", "foot_r"]
PARTS_2D_ES = {"head": "cabeza", "torso": "torso", "arm_l": "brazo izq.", "arm_r": "brazo der.",
               "hand_l": "mano izq.", "hand_r": "mano der.", "leg_l": "pierna izq.", "leg_r": "pierna der.",
               "foot_l": "pie izq.", "foot_r": "pie der."}

# Pieza 3D de la plantilla -> parte 2D (para los tests y para el ajuste por partes de la fase 4).
PART3D_TO_2D = {
    **{p: "head" for p in ("Head", "Hair", "Face", "Eye_L", "Eye_R", "MouthBag", "Teeth_Upper", "Teeth_Lower",
                           "Ear_L", "Ear_R")},
    **{p: "torso" for p in ("Neck", "Chest", "Abdomen", "Hips", "Shoulder_L", "Shoulder_R")},
    **{f"{a}_{s.upper()}": f"{b}_{s}" for s in ("l", "r") for a, b in (
        ("UpperArm", "arm"), ("Forearm", "arm"), ("Hand", "hand"), ("Thigh", "leg"), ("Shin", "leg"), ("Foot", "foot"))},
}

MATERIAL_KINDS = ["skin", "hair", "garment", "footwear", "accessory"]
MATERIAL_ES = {"skin": "piel", "hair": "pelo", "garment": "prenda", "footwear": "calzado", "accessory": "accesorio"}


def kind_base(kind: str) -> str:
    return kind.split("_")[0]


def kind_label(kind: str) -> str:
    base, _, n = kind.partition("_")
    return MATERIAL_ES.get(base, base) + (f" {n}" if n else "")


# ---------------------------------------------------------------------------- partes 2D
def _bones(lm: Landmarks2D) -> dict[str, list[tuple[np.ndarray, np.ndarray]]]:
    """Segmentos del esqueleto 2D que definen cada parte."""
    P = {k: np.asarray(v, float) for k, v in lm.all_points().items()}
    out: dict = {"head": [(np.array([P["crown"][0], lm.top]), P["chin"])],
                 "torso": [((P["shoulder_l"] + P["shoulder_r"]) / 2, P["pelvis"]), (P["shoulder_r"], P["shoulder_l"]),
                           (P["neck"], (P["shoulder_l"] + P["shoulder_r"]) / 2)]}
    for s in ("l", "r"):
        out[f"arm_{s}"] = [(P[f"shoulder_{s}"], P[f"wrist_{s}"])]
        out[f"hand_{s}"] = [(P[f"wrist_{s}"], P[f"fingertip_{s}"])]
        out[f"leg_{s}"] = [(P[f"hip_{s}"], P[f"ankle_{s}"])]
        out[f"foot_{s}"] = [(P[f"ankle_{s}"], np.array([P[f"ankle_{s}"][0], lm.bottom]))]
    return out


def _line_img(shape, segs, thickness=1) -> np.ndarray:
    img = np.zeros(shape, np.uint8)
    for a, b in segs:
        cv2.line(img, tuple(np.round(a).astype(int)), tuple(np.round(b).astype(int)), 255, thickness)
    return img


def parts_2d(rgb: np.ndarray, mask: np.ndarray, lm: Landmarks2D, edges: np.ndarray | None = None) -> np.ndarray:
    """Cada píxel de la silueta va a la parte cuyo hueso 2D está más cerca, con la distancia
    normalizada por el radio local de la extremidad (diagrama de potencia). Así el costado del
    torso no se lo queda el brazo.

    Después, en una banda estrecha alrededor de las fronteras y sólo cerca de bordes de Canny, un
    watershed ajusta el corte al contorno real (brazo delante del torso, borde de la manga).
    """
    h, w = mask.shape
    dt_mask = cv2.distanceTransform(mask, cv2.DIST_L2, 5)

    def radius(segs, default, q=50):
        on = (_line_img((h, w), segs) > 0) & (mask > 0)
        return float(np.percentile(dt_mask[on], q)) if on.any() else default

    bones = _bones(lm)
    # Manos: percentil alto, porque la palma es ancha y los dedos finos.
    r = {name: radius(bones[name][:1], 0.05 * lm.height_px, 90 if name.startswith("hand") else 50)
         for name in PARTS_2D}
    for s in ("l", "r"):  # mismo radio a ambos lados del tobillo: corte horizontal, no en V
        r[f"foot_{s}"] = max(r[f"foot_{s}"], r[f"leg_{s}"])
    dist = np.empty((len(PARTS_2D), h, w), np.float32)
    for i, name in enumerate(PARTS_2D):
        dist[i] = cv2.distanceTransform(255 - _line_img((h, w), bones[name][:1]), cv2.DIST_L2, 5) / max(r[name], 2.0)
    # Segmentos auxiliares del torso con su propio radio: la línea de hombros usa el del brazo
    # (con el del torso se comería los brazos) y el cuello el suyo (si no, se comería la mandíbula).
    t = PARTS_2D.index("torso")
    for seg, rad in ((bones["torso"][1], (r["arm_l"] + r["arm_r"]) / 2), (bones["torso"][2], None)):
        rad = rad or radius([seg], r["head"] / 2)
        aux = cv2.distanceTransform(255 - _line_img((h, w), [seg]), cv2.DIST_L2, 5)
        dist[t] = np.minimum(dist[t], aux / max(rad, 2.0))
    lab = np.argmin(dist, 0).astype(np.int32)
    lab[mask == 0] = -1

    band_w = max(2, int(0.01 * lm.height_px))
    # Fronteras entre partes (no el contorno de la silueta): máx. y mín. de vecinos ignorando el fondo.
    k3 = np.ones((3, 3), np.uint8)
    a = (lab + 1).astype(np.uint8)
    hi = a.copy()
    hi[mask == 0] = 255
    lo_n = cv2.erode(hi, k3)
    boundary = (cv2.dilate(a, k3) != lo_n) & (lo_n != 255) & (mask > 0)
    band = cv2.dilate(boundary.astype(np.uint8), np.ones((2 * band_w + 1,) * 2, np.uint8)) > 0
    if edges is None:
        return lab.astype(np.int8)
    # Sólo se reajusta donde hay un borde real cerca; en zonas lisas el watershed deja dientes.
    # Muñecas y tobillos se quedan con el corte geométrico: el sombreado de la mano no es un borde.
    kb = np.ones((2 * band_w + 1,) * 2, np.uint8)
    band &= cv2.dilate(edges, kb) > 0
    ends = np.isin(lab, [PARTS_2D.index(n) for n in ("hand_l", "hand_r", "foot_l", "foot_r")])
    band &= cv2.dilate(ends.astype(np.uint8), kb) == 0
    markers = lab + 1
    markers[band & (mask > 0)] = 0
    markers[mask == 0] = len(PARTS_2D) + 1
    img = smooth_for_segmentation(rgb).copy()
    img[edges > 0] = 255
    cv2.watershed(cv2.cvtColor(img, cv2.COLOR_RGB2BGR), markers)
    out = markers - 1
    out[(out < 0) | (out >= len(PARTS_2D)) | (mask == 0)] = -1
    holes = (out < 0) & (mask > 0)
    out[holes] = lab[holes]
    return out.astype(np.int8)


# ---------------------------------------------------------------------- materiales
L_WEIGHT = 0.5  # la iluminación cambia sobre todo L: el color (a, b) pesa más que la luminosidad


def material_clusters(rgb: np.ndarray, mask: np.ndarray, k: int = 8, max_samples: int = 60000, seed: int = 0):
    """k-means en Lab (L atenuada) sobre la silueta. Devuelve (etiquetas (h,w) int16, centros).

    El borde antialiasado (mezcla con el fondo) no entra en el k-means: cada píxel del borde toma
    la etiqueta del píxel interior más cercano.
    """
    lab = cv2.cvtColor(rgb, cv2.COLOR_RGB2LAB).reshape(-1, 3).astype(np.float32)
    lab[:, 0] *= L_WEIGHT
    inner = cv2.erode(mask, np.ones((5, 5), np.uint8))
    if not inner.any():
        inner = mask
    idx = np.flatnonzero(inner.ravel())
    rng = np.random.default_rng(seed)
    sample = lab[idx if len(idx) <= max_samples else rng.choice(idx, max_samples, replace=False)]
    cv2.setRNGSeed(seed)
    crit = (cv2.TERM_CRITERIA_EPS + cv2.TERM_CRITERIA_MAX_ITER, 20, 1.0)
    _, _, centers = cv2.kmeans(sample, k, None, crit, 3, cv2.KMEANS_PP_CENTERS)
    labels = np.empty(len(idx), np.int16)
    for a in range(0, len(idx), 200000):
        chunk = lab[idx[a:a + 200000]]
        labels[a:a + 200000] = np.argmin(((chunk[:, None, :] - centers[None]) ** 2).sum(2), 1)
    out = np.full(mask.size, -1, np.int16)
    out[idx] = labels
    out = out.reshape(mask.shape)
    rim = (mask > 0) & (inner == 0)
    if rim.any():
        _, (iy, ix) = distance_transform_edt(inner == 0, return_indices=True)
        out[rim] = out[iy[rim], ix[rim]]
    return out, centers


def skin_mask(rgb: np.ndarray) -> np.ndarray:
    """Rango clásico de piel en CrCb."""
    ycrcb = cv2.cvtColor(rgb, cv2.COLOR_RGB2YCrCb)
    return cv2.inRange(ycrcb, (0, 133, 77), (255, 173, 127)) > 0


def classify_materials(rgb: np.ndarray, labels: np.ndarray, centers: np.ndarray, parts: np.ndarray,
                       lm: Landmarks2D) -> tuple[np.ndarray, np.ndarray, list[str]]:
    """Reglas sobre los clusters: piel (cara/manos), pelo (sobre el cráneo), calzado (pies), prendas.

    Un cluster oscuro puede ser a la vez el pelo y el pantalón: si está en lo alto de la cabeza y
    también fuera de ella, se parte en dos. Devuelve (etiquetas, centros, tipos) actualizados.
    """
    labels = labels.copy()
    centers = np.asarray(centers, np.float32).copy()
    h, w = labels.shape
    skin = skin_mask(rgb)
    P = lm.all_points()
    yy, xx = np.mgrid[0:h, 0:w]
    eyes = (np.asarray(P["eye_l"]) + np.asarray(P["eye_r"])) / 2
    fc = (eyes + np.asarray(P["mouth"])) / 2
    fr = max(4.0, 0.22 * lm.head_width)
    face = ((xx - fc[0]) ** 2 + (yy - fc[1]) ** 2 <= fr ** 2) & (labels >= 0)
    head = parts == PARTS_2D.index("head")
    head_top = head & (yy < lm.top + 0.2 * lm.head_height)
    hands = np.isin(parts, [PARTS_2D.index("hand_l"), PARTS_2D.index("hand_r")])
    feet = np.isin(parts, [PARTS_2D.index("foot_l"), PARTS_2D.index("foot_r")])
    lab_img = cv2.cvtColor(rgb, cv2.COLOR_RGB2LAB).astype(np.float32)

    def share(region, c):        # qué parte de la región ocupa el cluster
        n = region.sum()
        return float((labels[region] == c).sum() / n) if n else 0.0

    def inside(region, c):       # qué parte del cluster cae en la región
        n = (labels == c).sum()
        return float((labels[region] == c).sum() / n) if n else 0.0

    for c in range(len(centers)):
        if share(head_top, c) >= 0.2 and inside(head, c) < 0.5:
            sel = (labels == c) & head
            labels[sel] = len(centers)
            mean = lab_img[sel].mean(0)
            mean[0] *= L_WEIGHT
            centers = np.vstack([centers, mean[None]])
    k = len(centers)

    def chroma(c, d):            # distancia sólo en (a, b): la sombra cambia L, no el tono
        return float(np.linalg.norm(centers[c, 1:] - centers[d, 1:]))

    skin_frac = [float(skin[labels == c].mean()) if (labels == c).any() else 0.0 for c in range(k)]
    kinds: list = [None] * k
    for c in range(k):
        if skin_frac[c] >= 0.5 and (share(face, c) >= 0.15 or share(hands, c) >= 0.25):
            kinds[c] = "skin"
    for c in range(k):
        if kinds[c] is None and share(feet, c) >= 0.15 and inside(feet, c) >= 0.5:
            kinds[c] = "footwear"
    for c in range(k):
        if kinds[c] is None and share(head_top, c) >= 0.2 and inside(head, c) >= 0.5:
            kinds[c] = "hair"
    skin_c = [c for c in range(k) if kinds[c] == "skin"]
    for c in range(k):
        if kinds[c] is None and skin_c and skin_frac[c] >= 0.6 and min(chroma(c, s) for s in skin_c) < 12:
            kinds[c] = "skin"
    rest = sorted((c for c in range(k) if kinds[c] is None), key=lambda c: -(labels == c).sum())
    groups: list[list[int]] = []
    for c in rest:  # mismo tono = la misma prenda con distinta iluminación
        g = next((g for g in groups if chroma(c, g[0]) < 8 and abs(centers[c, 0] - centers[g[0], 0]) < 25), None)
        if g is None:
            groups.append([c])
        else:
            g.append(c)
    for i, g in enumerate(groups):
        for c in g:
            kinds[c] = f"garment_{i + 1}"
    return labels, centers, kinds


def cluster_rgb(rgb: np.ndarray, labels: np.ndarray, k: int) -> np.ndarray:
    out = np.zeros((k, 3), np.uint8)
    for c in range(k):
        sel = labels == c
        if sel.any():
            out[c] = np.median(rgb[sel], axis=0)
    return out
