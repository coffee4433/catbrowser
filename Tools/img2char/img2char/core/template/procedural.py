"""Maniquí procedural con topología de plantilla (README §3).

Sustituto provisional de la plantilla MakeHuman: una jaula de quads modelada por código
(extrusiones como en box modeling) + Catmull–Clark + detalles de cara/manos + otro nivel de
subdivisión. Masculino y femenino comparten topología porque todas las selecciones
geométricas se calculan una vez sobre la malla canónica y se reproducen por índice.
"""
from __future__ import annotations

from dataclasses import dataclass, field, replace

import numpy as np

from ..mesh.quadmesh import QuadMesh, ring_transform, scale_transform, unit


@dataclass(frozen=True)
class BodyParams:
    """Parámetros del generador. Los de proporción están en unidades de morph (1 = +8–10 %)."""
    height: float = 178.0
    female: float = 0.0
    shoulder_width: float = 0.0
    hip_width: float = 0.0
    leg_length: float = 0.0
    arm_length: float = 0.0
    torso_length: float = 0.0
    head_size: float = 0.0
    fat: float = 0.0
    muscle: float = 0.0


MALE = BodyParams()
FEMALE = BodyParams(height=166.0, female=1.0)


class Selections(dict):
    """Caché de selecciones por nombre: la primera construcción las calcula, el resto las reutiliza."""

    def get_or(self, key: str, fn):
        if key not in self:
            self[key] = [int(x) for x in fn()]
        return self[key]


@dataclass
class BuildResult:
    V: np.ndarray
    F: np.ndarray
    labels: list
    groups: dict
    loops: dict
    info: dict = field(default_factory=dict)


# Tronco: 8 anillos de 3×1 caras (x: derecha, centro, izquierda; z: espalda, frente).
TORSO_T = [0.0, 0.12, 0.28, 0.42, 0.56, 0.70, 0.84, 1.0]
TORSO_LABEL = [None, "Hips", "Hips", "Abdomen", "Abdomen", "Chest", "Chest", "Chest"]
W_M = [0.090, 0.100, 0.094, 0.086, 0.090, 0.100, 0.108, 0.100]
W_F = [0.096, 0.112, 0.102, 0.080, 0.082, 0.090, 0.094, 0.090]
ZF_M = [0.040, 0.056, 0.060, 0.060, 0.064, 0.074, 0.072, 0.052]
ZF_F = [0.040, 0.054, 0.056, 0.052, 0.056, 0.064, 0.062, 0.046]
ZB_M = [-0.062, -0.072, -0.062, -0.054, -0.052, -0.060, -0.062, -0.054]
ZB_F = [-0.070, -0.084, -0.066, -0.052, -0.050, -0.054, -0.056, -0.050]

FINGER_SPECS = {  # nombre: (desplazamiento a lo ancho, abertura, longitud relativa)
    "index": (0.75, 0.10, 0.95),
    "middle": (0.25, 0.03, 1.00),
    "ring": (-0.25, -0.04, 0.94),
    "pinky": (-0.75, -0.11, 0.76),
}
PHALANX = (0.45, 0.75, 1.0)  # posiciones acumuladas de las articulaciones a lo largo del dedo


def _lerp(a, b, t):
    return np.asarray(a, float) + (np.asarray(b, float) - np.asarray(a, float)) * t


def _sides(lr: str) -> tuple[int, str]:
    return (1, "L") if lr == "L" else (-1, "R")


def build_body(p: BodyParams, sel: Selections, levels: tuple[int, int] = (2, 1)) -> BuildResult:
    H, f = p.height, p.female
    hs = 1 + 0.10 * p.head_size
    armf = 1 + 0.08 * p.arm_length
    legf = 1 + 0.08 * p.leg_length
    fat, mus = p.fat, p.muscle
    m = QuadMesh()
    info: dict = {}

    # ------------------------------------------------------------------ tronco
    crotch = 0.475 * H * legf
    torso_len = 0.335 * H * (1 + 0.08 * p.torso_length)
    hipf, shf = 1 + 0.1 * p.hip_width, 1 + 0.1 * p.shoulder_width
    W, ZF, ZB = _lerp(W_M, W_F, f) * H, _lerp(ZF_M, ZF_F, f) * H, _lerp(ZB_M, ZB_F, f) * H
    M = np.linspace(0.016, 0.036, 8) * H
    Y = crotch + np.asarray(TORSO_T) * torso_len
    for k in range(8):
        wf = hipf if k <= 2 else shf if k >= 5 else (hipf + shf) / 2
        W[k] *= wf * (1 + 0.10 * fat + (0.06 * mus if k >= 5 else 0))
        ZF[k] *= 1 + 0.14 * fat * (1.0 if 1 <= k <= 5 else 0.4) + (0.08 * mus if k >= 5 else 0)
        ZB[k] *= 1 + 0.08 * fat

    def grid_pos(k, ix, iz):
        return np.array([[-W[k], -M[k], M[k], W[k]][ix], Y[k], [ZB[k], ZF[k]][iz]])

    g0 = {(ix, iz): m.add_vertex(grid_pos(0, ix, iz)) for ix in range(4) for iz in range(2)}
    cells = [[g0[(ix, 0)], g0[(ix + 1, 0)], g0[(ix + 1, 1)], g0[(ix, 1)]] for ix in range(3)]
    region = [m.add_face(c, "Hips", outward=(0, 1, 0)) for c in cells]
    g = dict(g0)
    last = None
    for k in range(1, 8):
        res = m.extrude(region, {g[c]: grid_pos(k, *c) for c in g}, label=TORSO_LABEL[k],
                        side_label=TORSO_LABEL[k])
        g = {c: res.mapping[v] for c, v in g.items()}
        last = res
    bottom = [m.add_face(c, "Hips", outward=(0, -1, 0)) for c in cells]
    info["torso_y"] = Y.tolist()

    # ------------------------------------------------------------- cuello y cabeza
    neck_face = region[1]
    m.extrude([neck_face], ring_transform((0, Y[7], -0.008 * H), (0, 1, 0), 0.034 * H, 0.036 * H, ref=(1, 0, 0)),
              side_label="Chest")
    m.extrude([neck_face], ring_transform((0, Y[7] + 0.03 * H, -0.012 * H), (0, 1, 0), 0.032 * H * (1 + 0.08 * fat),
                                          0.034 * H * (1 + 0.08 * fat), ref=(1, 0, 0)), label="Neck")
    radii = np.array([0.058, 0.073, 0.066]) * H * hs
    hc = np.array([0.0, Y[7] + 0.045 * H + 0.80 * radii[1], 0.010 * H])
    grid = [-1.0, -0.42, 0.42, 1.0]

    def head_pos(i, j, k):
        q = np.array([grid[i], grid[j], grid[k]])
        d = q / (np.abs(q) ** 2.4).sum() ** (1 / 2.4)
        d[0] *= 1 - 0.20 * max(0.0, -d[1]) * (0.5 + 0.5 * max(0.0, d[2]))  # mandíbula más estrecha
        return hc + d * radii

    def head_label(side, a, b):
        if side == "+z":
            return "Face"
        if side == "-y" and (a, b) == (1, 1):
            return None
        return "Head"

    ids = m.box_grid((3, 3, 3), head_pos, head_label)
    hole = [ids[(1, 0, 1)], ids[(2, 0, 1)], ids[(2, 0, 2)], ids[(1, 0, 2)]]
    m.bridge(neck_face, hole, "Neck")
    info["head_center"], info["head_radii"] = hc.tolist(), radii.tolist()

    # ------------------------------------------------------------------- brazos
    a45 = np.sqrt(0.5)
    for lr in ("L", "R"):
        s, S = _sides(lr)
        side_face = sel.get_or(f"clavicle_{lr}", lambda: [max(last.sides, key=lambda fi: m.face_normal(fi)[0] * s)])
        c0 = m.face_center(side_face[0])
        girth = 1 + 0.06 * fat
        shoulder_c = c0 + np.array([s * 0.036 * H, -0.008 * H, -0.004 * H])
        m.extrude(side_face, ring_transform(shoulder_c, (s, -0.35, 0), 0.040 * H * (1 + 0.08 * mus) * girth,
                                            0.040 * H * (1 + 0.08 * mus) * girth), label=f"Shoulder_{S}")
        a = np.array([s * a45, -a45, 0.0])
        Lu, Lf = 0.165 * H * armf, 0.145 * H * armf
        up = 1 + 0.12 * mus + 0.10 * fat
        elbow = shoulder_c + a * Lu
        wrist = elbow + a * Lf
        m.loft(side_face, [
            dict(center=shoulder_c + a * Lu * 0.45, axis=a, hu=0.036 * H * up, hv=0.034 * H * up),
            dict(center=elbow, axis=a, hu=0.028 * H * girth, hv=0.026 * H * girth),
        ], label=f"UpperArm_{S}")
        m.loft(side_face, [
            dict(center=elbow + a * Lf * 0.35, axis=a, hu=0.030 * H * girth, hv=0.027 * H * girth),
            dict(center=wrist, axis=a, hu=0.024 * H, hv=0.0135 * H),
        ], label=f"Forearm_{S}")
        hand = m.loft(side_face, [
            dict(center=wrist + a * 0.040 * H, axis=a, hu=0.037 * H, hv=0.018 * H),
            dict(center=wrist + a * 0.080 * H, axis=a, hu=0.035 * H, hv=0.016 * H),
        ], label=f"Hand_{S}")
        m.loops[f"knuckle_{lr.lower()}"] = hand[-1].loop_new
        info[f"arm_axis_{lr.lower()}"] = a.tolist()

    # -------------------------------------------------------------------- piernas
    knee_y, ankle_y = crotch * 0.58, 0.055 * H
    for lr, bf in (("L", bottom[2]), ("R", bottom[0])):
        s, S = _sides(lr)
        thick = 1 + 0.12 * fat + 0.10 * mus
        down = (0, -1, 0)
        m.loft([bf], [
            dict(center=(s * 0.058 * H, crotch - 0.13 * H * legf, 0.004 * H), axis=down,
                 hu=0.052 * H * thick, hv=0.058 * H * thick),
            dict(center=(s * 0.056 * H, knee_y + 0.01 * H, 0.006 * H), axis=down, hu=0.034 * H, hv=0.038 * H),
        ], label=f"Thigh_{S}", ref=(1, 0, 0))
        m.loft([bf], [
            dict(center=(s * 0.054 * H, knee_y - 0.09 * H * legf, -0.006 * H), axis=down,
                 hu=0.036 * H * (1 + 0.08 * fat), hv=0.044 * H * (1 + 0.08 * fat)),
            dict(center=(s * 0.052 * H, ankle_y, -0.012 * H), axis=down, hu=0.021 * H, hv=0.024 * H),
        ], label=f"Shin_{S}", ref=(1, 0, 0))
        heel = m.extrude([bf], ring_transform((s * 0.052 * H, 0.014 * H, -0.004 * H), down, 0.028 * H, 0.046 * H,
                                              ref=(1, 0, 0)), label=f"Foot_{S}")
        front = sel.get_or(f"foot_front_{lr}", lambda: [max(heel.sides, key=lambda fi: m.face_normal(fi)[2])])
        toes = m.loft(front, [
            dict(center=(s * 0.054 * H, 0.022 * H, 0.072 * H), axis=(0, 0, 1), hu=0.031 * H, hv=0.017 * H),
            dict(center=(s * 0.056 * H, 0.014 * H, 0.105 * H), axis=(0, 0, 1), hu=0.027 * H, hv=0.011 * H),
        ], label=f"Foot_{S}", ref=(1, 0, 0))
        m.loops[f"ball_{lr.lower()}"] = toes[0].loop_new

    for _ in range(levels[0]):
        m = m.subdivided()

    _add_face_details(m, sel, H, hs, info)
    _add_fingers(m, sel, H, armf, info)

    for _ in range(levels[1]):
        m = m.subdivided()

    if f:
        _female_shape(m, H, f, crotch, torso_len)
    _add_islands(m, sel, H, hs, info)
    V = m.V
    V[:, 1] -= V[:, 1].min()  # la subdivisión levanta la suela: pies en y = 0
    m.verts = list(V)

    V, F, labels, groups, loops = m.compact()
    return BuildResult(V, F, labels, groups, loops, info)


def _front_faces(m: QuadMesh, label: str, min_nz: float = 0.3) -> list[int]:
    return [fi for fi in m.faces_labeled(label) if m.face_normal(fi)[2] > min_nz]


def _add_face_details(m: QuadMesh, sel: Selections, H: float, hs: float, info: dict) -> None:
    hc = np.asarray(info["head_center"])
    face_V = m.V[np.unique([v for fi in m.faces_labeled("Face") for v in m.F[fi]])]
    front_z = face_V[:, 2].max()
    eps = 0.001 * H  # desempate hacia -x para regiones centradas en el plano de simetría

    for lr in ("L", "R"):
        s, _ = _sides(lr)
        rect = sel.get_or(f"eye_{lr}", lambda: m.select_rect(
            (s * 0.0185 * H * hs, hc[1] + 0.004 * H * hs, front_z), (1, 0, 0), (0, 1, 0), 3, 1,
            _front_faces(m, "Face")))
        res = m.extrude(rect, scale_transform((1.05, 1.25, 1.0), (0, 0, -0.006 * H * hs)), label="Face")
        m.loops[f"eye_open_{lr.lower()}"] = res.loop_old
        m.loops[f"eye_socket_{lr.lower()}"] = res.loop_new
        m.delete_faces(rect)

    nose = sel.get_or("nose", lambda: m.select_rect(
        (-eps, hc[1] - 0.013 * H * hs, front_z), (1, 0, 0), (0, 1, 0), 2, 2, _front_faces(m, "Face")))
    m.extrude(nose, scale_transform((0.7, 0.9, 1.0), (0, -0.004 * H * hs, 0.012 * H * hs)), label="Face")
    m.extrude(nose, scale_transform((0.75, 0.7, 1.0), (0, -0.002 * H * hs, 0.004 * H * hs)), label="Face")

    mouth = sel.get_or("mouth", lambda: m.select_rect(
        (-eps, hc[1] - 0.033 * H * hs, front_z), (1, 0, 0), (0, 1, 0), 4, 1, _front_faces(m, "Face")))
    lips = m.extrude(mouth, scale_transform((0.92, 0.25, 1.0), (0, 0, -0.003 * H * hs)), label="MouthBag")
    m.loops["lips_inner"] = lips.loop_old
    m.extrude(mouth, scale_transform((1.0, 7.0, 1.0), (0, 0, -0.012 * H * hs)), label="MouthBag")
    m.extrude(mouth, scale_transform((0.8, 0.8, 1.0), (0, 0, -0.012 * H * hs)), label="MouthBag")

    for lr in ("L", "R"):
        s, S = _sides(lr)
        head_V = m.V[np.unique([v for fi in m.faces_labeled("Head") for v in m.F[fi]])]
        side_x = np.abs(head_V[:, 0]).max()
        ear = sel.get_or(f"ear_{lr}", lambda: m.select_rect(
            (s * side_x, hc[1] - 0.004 * H * hs, hc[2] - 0.010 * H * hs), (0, 1, 0), (0, 0, 1), 3, 2,
            [fi for fi in m.faces_labeled("Head") if m.face_normal(fi)[0] * s > 0.5]))
        m.extrude(ear, scale_transform((1.0, 1.1, 0.8), (s * 0.006 * H * hs, 0.001 * H, -0.002 * H)), label=f"Ear_{S}")
        m.extrude(ear, scale_transform((1.0, 1.15, 1.1), (s * 0.005 * H * hs, 0.002 * H, -0.005 * H)), label=f"Ear_{S}")


def _add_fingers(m: QuadMesh, sel: Selections, H: float, armf: float, info: dict) -> None:
    z = np.array([0.0, 0.0, 1.0])
    for lr in ("L", "R"):
        s, S = _sides(lr)
        a = np.asarray(info[f"arm_axis_{lr.lower()}"])
        v = s * np.cross(a, z)  # normal de la palma (simétrica entre lados)
        ring = m.V[m.loops[f"knuckle_{lr.lower()}"]]
        kc = ring.mean(0)
        hw = np.abs((ring - kc) @ z).max()
        hand_faces = m.faces_labeled(f"Hand_{S}")
        tip = kc + a * 0.012 * H
        used: set = set()
        for name, (off, spread, length) in FINGER_SPECS.items():
            rect = sel.get_or(f"{name}_{lr}", lambda: m.select_rect(
                tip + z * off * hw * 0.85 + v * 0.0005 * H, z, v, 1, 2,
                [fi for fi in hand_faces if m.face_normal(fi) @ a > 0.4]))
            if used & set(rect):
                raise ValueError(f"dedos solapados en la mano {lr}")
            used |= set(rect)
            c0 = m.V[np.unique([vv for fi in rect for vv in m.F[fi]])].mean(0)
            d = unit(a + z * spread)
            L = 0.052 * H * armf * length
            faces = set(rect)
            for k, (t, w) in enumerate(zip(PHALANX, (0.0068, 0.0062, 0.0054))):
                res = m.extrude(rect, ring_transform(c0 + d * L * t, d, w * H, w * H * 0.85), label=f"Hand_{S}")
                if k == 0:
                    m.loops[f"{name}_0_{lr.lower()}"] = res.loop_old
                m.loops[f"{name}_{k + 1}_{lr.lower()}"] = res.loop_new
                faces |= set(res.sides)
            m.add_to_group(f"{name}_{lr.lower()}", faces)

        thumb = sel.get_or(f"thumb_{lr}", lambda: m.select_rect(
            kc - a * 0.05 * H + z * hw, a, v, 2, 1,
            [fi for fi in hand_faces if m.face_normal(fi) @ z > 0.5 and fi not in used]))
        c0 = m.V[np.unique([vv for fi in thumb for vv in m.F[fi]])].mean(0)
        d = unit(a * 0.55 + z * 0.75 + v * 0.35)
        L = 0.046 * H * armf
        faces = set(thumb)
        for k, (t, w) in enumerate(zip((0.40, 0.72, 1.0), (0.0085, 0.0075, 0.0065))):
            res = m.extrude(thumb, ring_transform(c0 + d * L * t, d, w * H, w * H * 0.85), label=f"Hand_{S}")
            if k == 0:
                m.loops[f"thumb_0_{lr.lower()}"] = res.loop_old
            m.loops[f"thumb_{k + 1}_{lr.lower()}"] = res.loop_new
            faces |= set(res.sides)
        m.add_to_group(f"thumb_{lr.lower()}", faces)


def _female_shape(m: QuadMesh, H: float, f: float, crotch: float, torso_len: float) -> None:
    V = m.V
    chest = np.unique([v for fi in m.faces_labeled("Chest") for v in m.F[fi]])
    hips = np.unique([v for fi in m.faces_labeled("Hips") for v in m.F[fi]])
    yb = crotch + 0.72 * torso_len
    d = unit([0.0, -0.25, 1.0])
    for s in (1, -1):
        c = np.array([s * 0.050 * H, yb, 0.0])
        P = V[chest]
        r2 = ((P[:, 0] - c[0]) ** 2 + (P[:, 1] - c[1]) ** 2) / (0.032 * H) ** 2
        w = np.exp(-r2) * (P[:, 2] > 0)
        V[chest] += np.outer(w * f * 0.030 * H, d)
        c = np.array([s * 0.045 * H, crotch + 0.08 * torso_len, 0.0])
        P = V[hips]
        r2 = ((P[:, 0] - c[0]) ** 2 + (P[:, 1] - c[1]) ** 2) / (0.045 * H) ** 2
        w = np.exp(-r2) * (P[:, 2] < 0)
        V[hips] += np.outer(w * f * 0.012 * H, [0.0, 0.0, -1.0])
    m.verts = list(V)


def _sphere_pos(center, radius, n):
    def pos(i, j, k):
        q = np.array([i, j, k], float) / n * 2 - 1
        return center + q / np.linalg.norm(q) * radius
    return pos


def _add_islands(m: QuadMesh, sel: Selections, H: float, hs: float, info: dict) -> None:
    """Ojos, dientes y pelo: islas independientes construidas sobre la malla final."""
    for lr in ("L", "R"):
        _, S = _sides(lr)
        opening = m.V[m.loops[f"eye_open_{lr.lower()}"]]
        re = 0.0068 * H * hs
        c = opening.mean(0) - np.array([0.0, 0.0, re * 0.70])
        ids = m.box_grid((4, 4, 4), _sphere_pos(c, re, 4), lambda *_: f"Eye_{S}")
        m.loops[f"eye_center_{lr.lower()}"] = sorted(ids.values())

    lc = m.V[m.loops["lips_inner"]].mean(0)
    ac = lc - np.array([0.0, 0.0, 0.015 * H * hs])
    R, span, t = 0.011 * H * hs, np.radians(60), 0.0025 * H * hs
    for name, (y0, y1) in (("Teeth_Upper", (0.0004, 0.0045)), ("Teeth_Lower", (-0.0045, -0.0004))):
        def pos(i, j, k, y0=y0, y1=y1):
            th = -span + 2 * span * i / 6
            r = R - t + t * k
            return ac + np.array([r * np.sin(th), (y0 + (y1 - y0) * j) * H * hs, r * np.cos(th)])
        m.box_grid((6, 1, 1), pos, lambda *_, name=name: name)

    hc, radii = np.asarray(info["head_center"]), np.asarray(info["head_radii"])

    def hair_faces():
        out = []
        for fi in m.faces_labeled("Head"):
            c = m.face_center(fi)
            if c[1] > hc[1] - 0.05 * radii[1] or (c[2] < hc[2] - 0.25 * radii[2] and c[1] > hc[1] - 0.50 * radii[1]):
                out.append(fi)
        return out

    region = sel.get_or("hair", hair_faces)
    m.shell(region, 0.0015 * H, 0.0085 * H, "Hair")


def symmetrize(V: np.ndarray, mirror: np.ndarray) -> np.ndarray:
    R = V[mirror] * np.array([-1.0, 1.0, 1.0])
    return (V + R) / 2


def mirror_map(V: np.ndarray, tol: float = 1e-3) -> np.ndarray:
    from scipy.spatial import cKDTree

    d, idx = cKDTree(V).query(V * np.array([-1.0, 1.0, 1.0]))
    if d.max() > tol:
        raise ValueError(f"la malla no es simétrica (error máx. {d.max():.4g})")
    if (idx[idx] != np.arange(len(V))).any():
        raise ValueError("mapa de simetría no involutivo")
    return idx


def with_param(p: BodyParams, name: str, delta: float) -> BodyParams:
    return replace(p, **{name: getattr(p, name) + delta})
