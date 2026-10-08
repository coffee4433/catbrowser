"""Ensambla las plantillas male_v1 / female_v1 a partir del generador procedural."""
from __future__ import annotations

import numpy as np

from .model import Template
from .parts_def import PART_INDEX, PARTS
from .procedural import FEMALE, MALE, BodyParams, Selections, build_body, mirror_map, symmetrize, with_param
from .rings import find_rings
from .skeleton import FINGERS, compute_skeleton
from .uv import unwrap

VERSION = 1

# (nombre, parámetro del generador, incremento por unidad, rango, descripción de la unidad)
MORPHS = [
    ("gender", "female", None, (0.0, 1.0), "0 = plantilla, 1 = sexo opuesto"),
    ("height", "height", 10.0, (-2.0, 2.0), "+10 cm"),
    ("shoulder_width", "shoulder_width", 1.0, (-1.5, 1.5), "+10 % hombros"),
    ("hip_width", "hip_width", 1.0, (-1.5, 1.5), "+10 % cadera"),
    ("leg_length", "leg_length", 1.0, (-1.5, 1.5), "+8 % piernas"),
    ("arm_length", "arm_length", 1.0, (-1.5, 1.5), "+8 % brazos"),
    ("torso_length", "torso_length", 1.0, (-1.5, 1.5), "+8 % tronco"),
    ("head_size", "head_size", 1.0, (-1.5, 1.5), "+10 % cabeza"),
    ("fat", "fat", 1.0, (0.0, 1.5), "volumen de grasa"),
    ("muscle", "muscle", 1.0, (0.0, 1.5), "volumen muscular"),
]


def _verts(F, part_id, *names) -> np.ndarray:
    mask = np.isin(part_id, [PART_INDEX[n] for n in names])
    return np.unique(F[mask])


def compute_landmarks(V, F, part_id, rings, loops) -> dict[str, int]:
    """~40 landmarks como índices de vértice, elegidos por reglas geométricas sobre la malla canónica."""
    lm: dict[str, int] = {}

    def pick(name, idx, score):
        idx = np.asarray(idx)
        lm[name] = int(idx[np.argmax(score(V[idx]))])

    head, face = _verts(F, part_id, "Head"), _verts(F, part_id, "Face")
    pick("crown", head, lambda P: P[:, 1])
    mid_face = face[np.abs(V[face, 0]) < 0.3]
    pick("chin", mid_face, lambda P: -P[:, 1] + 0.2 * P[:, 2])
    pick("nose_tip", face, lambda P: P[:, 2])
    eye_y = V[loops["eye_open_l"]][:, 1].mean()
    pick("glabella", mid_face, lambda P: P[:, 2] - 5 * np.abs(P[:, 1] - (eye_y + 1.2)))
    pick("neck_front", _verts(F, part_id, "Neck"), lambda P: P[:, 2])
    pick("sternum", rings["neck_base"]["loop"], lambda P: P[:, 2])
    navel_y = V[rings["waist"]["loop"]][:, 1].mean() * 0.4 + V[rings["pelvis_top"]["loop"]][:, 1].mean() * 0.6
    pick("navel", _verts(F, part_id, "Abdomen"), lambda P: P[:, 2] - 5 * np.abs(P[:, 1] - navel_y) - 5 * np.abs(P[:, 0]))
    pick("crotch", _verts(F, part_id, "Hips"), lambda P: -P[:, 1] - 5 * np.abs(P[:, 0]))
    for s, lr in ((1, "l"), (-1, "r")):
        S = lr.upper()
        pick(f"eye_outer_{lr}", loops[f"eye_open_{lr}"], lambda P: s * P[:, 0])
        pick(f"eye_inner_{lr}", loops[f"eye_open_{lr}"], lambda P: -s * P[:, 0])
        pick(f"mouth_corner_{lr}", rings["lips_inner"]["loop"], lambda P: s * P[:, 0])
        pick(f"cheekbone_{lr}", face, lambda P: s * P[:, 0] * 0.3 + P[:, 2] - 4 * np.abs(P[:, 1] - (eye_y - 1.5)))
        pick(f"ear_lobe_{lr}", _verts(F, part_id, f"Ear_{S}"), lambda P: -P[:, 1])
        pick(f"acromion_{lr}", rings[f"upperarm_{lr}"]["loop"], lambda P: P[:, 1])
        pick(f"elbow_{lr}", rings[f"elbow_{lr}"]["loop"], lambda P: -P[:, 2])
        pick(f"wrist_{lr}", rings[f"wrist_{lr}"]["loop"], lambda P: s * P[:, 0] - P[:, 1])
        knuckle = V[loops[f"middle_0_{lr}"]].mean(0)
        pick(f"knuckle_{lr}", loops[f"middle_0_{lr}"], lambda P: -np.linalg.norm(P - knuckle, axis=1))
        pick(f"fingertip_{lr}", _verts(F, part_id, f"Hand_{S}"), lambda P: s * P[:, 0] - P[:, 1])
        pick(f"iliac_crest_{lr}", rings["pelvis_top"]["loop"], lambda P: s * P[:, 0])
        pick(f"trochanter_{lr}", _verts(F, part_id, "Hips"), lambda P: s * P[:, 0])
        pick(f"knee_{lr}", rings[f"knee_{lr}"]["loop"], lambda P: P[:, 2])
        pick(f"ankle_{lr}", rings[f"ankle_{lr}"]["loop"], lambda P: s * P[:, 0])
        foot = _verts(F, part_id, f"Foot_{S}")
        pick(f"heel_{lr}", foot, lambda P: -P[:, 2] - P[:, 1])
        pick(f"toe_tip_{lr}", foot, lambda P: P[:, 2])
    return lm


def compute_groups(V, F, part_id, face_groups, rings, loops) -> dict:
    groups: dict = {}
    for lr in ("l", "r"):
        S = lr.upper()
        hand = set(np.flatnonzero(part_id == PART_INDEX[f"Hand_{S}"]).tolist())
        g = {f: sorted(face_groups[f"{f}_{lr}"]) for f in FINGERS}
        g["palm"] = sorted(hand - set().union(*map(set, g.values())))
        groups[f"Hand_{S}"] = {k: g[k] for k in ("palm", *FINGERS)}
        foot = np.flatnonzero(part_id == PART_INDEX[f"Foot_{S}"])
        zc = V[F[foot]].mean(1)[:, 2]
        za = V[rings[f"ankle_{lr}"]["loop"]][:, 2].mean()
        zb = V[loops[f"ball_{lr}"]][:, 2].mean()
        groups[f"Foot_{S}"] = {"heel": foot[zc < za].tolist(), "body": foot[(zc >= za) & (zc <= zb)].tolist(),
                               "toes": foot[zc > zb].tolist()}
    return groups


def build_templates(levels: tuple[int, int] = (2, 1)) -> dict[str, Template]:
    sel = Selections()
    canon = build_body(MALE, sel, levels)
    F = canon.F
    part_id = np.array([PART_INDEX[lab] for lab in canon.labels])
    mirror = mirror_map(canon.V)
    Vc = symmetrize(canon.V, mirror)
    rings = find_rings(Vc, F, part_id)
    uv, uv_faces = unwrap(Vc, F, part_id)
    landmarks = compute_landmarks(Vc, F, part_id, rings, canon.loops)
    groups = compute_groups(Vc, F, part_id, canon.groups, rings, canon.loops)
    ring_loops = {k: r["loop"] for k, r in rings.items()}

    def body(p: BodyParams) -> np.ndarray:
        r = build_body(p, sel, levels)
        if r.F.shape != F.shape or (r.F != F).any():
            raise RuntimeError("la topología cambió entre construcciones")
        return symmetrize(r.V, mirror)

    out = {}
    for name, params in (("male_v1", MALE), ("female_v1", FEMALE)):
        V0 = body(params)
        T, info = [], []
        for mname, field_, step, (lo, hi), unit in MORPHS:
            if step is None:
                step = 1.0 if params.female < 0.5 else -1.0
            T.append(body(with_param(params, field_, step)) - V0)
            info.append({"name": mname, "lo": lo, "hi": hi, "unit": unit})
        out[name] = Template(
            name=name, V0=V0, F=F, part_id=part_id, uv=uv, uv_faces=uv_faces, mirror=mirror,
            landmarks=landmarks, rings=rings, loops=canon.loops, groups=groups,
            skeleton=compute_skeleton(V0, ring_loops, canon.loops),
            morph_names=[i["name"] for i in info], morph_T=np.stack(T), morph_info=info,
            meta={"source": "procedural", "version": VERSION, "levels": list(levels),
                  "params": params.__dict__, "units": "cm", "up_axis": "Y", "front_axis": "+Z",
                  "mirror_axis": "X", "parts": [p.id for p in PARTS]},
        )
    return out
