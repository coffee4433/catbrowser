"""Esqueleto con nombres del maniquí de UE5 (README §10.1) y posición de articulaciones (§10.2)."""
from __future__ import annotations

import numpy as np

FINGERS = ("thumb", "index", "middle", "ring", "pinky")


def bone_hierarchy() -> list[tuple[str, str | None]]:
    bones = [("root", None), ("pelvis", "root")]
    prev = "pelvis"
    for i in range(1, 6):
        bones.append((f"spine_0{i}", prev))
        prev = f"spine_0{i}"
    bones += [("neck_01", "spine_05"), ("neck_02", "neck_01"), ("head", "neck_02"),
              ("eye_l", "head"), ("eye_r", "head"), ("jaw", "head")]
    for s in ("l", "r"):
        bones += [(f"clavicle_{s}", "spine_05"), (f"upperarm_{s}", f"clavicle_{s}"),
                  (f"lowerarm_{s}", f"upperarm_{s}"), (f"hand_{s}", f"lowerarm_{s}")]
        for f in FINGERS:
            parent = f"hand_{s}"
            if f != "thumb":
                bones.append((f"{f}_metacarpal_{s}", parent))
                parent = f"{f}_metacarpal_{s}"
            for k in (1, 2, 3):
                bones.append((f"{f}_0{k}_{s}", parent))
                parent = f"{f}_0{k}_{s}"
    for s in ("l", "r"):
        bones += [(f"thigh_{s}", "pelvis"), (f"calf_{s}", f"thigh_{s}"),
                  (f"foot_{s}", f"calf_{s}"), (f"ball_{s}", f"foot_{s}")]
    return bones


def compute_skeleton(V: np.ndarray, rings: dict[str, list[int]], loops: dict[str, list[int]]) -> list[dict]:
    """Articulación = centroide del anillo compartido; columna interpolada entre anillos del tronco."""

    def c(name: str) -> np.ndarray:
        idx = rings.get(name) or loops[name]
        return V[idx].mean(0)

    pos: dict[str, np.ndarray] = {}
    pos["root"] = np.array([0.0, 0.0, 0.0])
    thighs = (c("thigh_l") + c("thigh_r")) / 2
    pos["pelvis"] = (thighs + c("pelvis_top")) / 2
    pt, wa, nb = c("pelvis_top"), c("waist"), c("neck_base")
    pos["spine_01"] = pt
    pos["spine_02"] = (pt + wa) / 2
    pos["spine_03"] = wa
    pos["spine_04"] = wa + (nb - wa) * 0.4
    pos["spine_05"] = wa + (nb - wa) * 0.75
    pos["neck_01"] = c("neck_base")
    pos["neck_02"] = (c("neck_base") + c("neck_top")) / 2
    pos["head"] = c("neck_top")
    pos["jaw"] = pos["head"] + (c("lips_inner") - pos["head"]) * 0.35
    for s in ("l", "r"):
        pos[f"eye_{s}"] = c(f"eye_center_{s}")
        pos[f"clavicle_{s}"] = c("neck_base") + (c(f"clavicle_{s}") - c("neck_base")) * 0.25
        pos[f"upperarm_{s}"] = c(f"upperarm_{s}")
        pos[f"lowerarm_{s}"] = c(f"elbow_{s}")
        pos[f"hand_{s}"] = c(f"wrist_{s}")
        for f in FINGERS:
            base = [c(f"{f}_{k}_{s}") for k in range(4)]
            if f != "thumb":
                pos[f"{f}_metacarpal_{s}"] = pos[f"hand_{s}"] + (base[0] - pos[f"hand_{s}"]) * 0.25
            for k in (1, 2, 3):
                pos[f"{f}_0{k}_{s}"] = base[k - 1]
        pos[f"thigh_{s}"] = c(f"thigh_{s}") + (pos["pelvis"] - thighs) * 0.5
        pos[f"calf_{s}"] = c(f"knee_{s}")
        pos[f"foot_{s}"] = c(f"ankle_{s}")
        pos[f"ball_{s}"] = c(f"ball_{s}")
    return [{"name": n, "parent": p, "head": [round(float(x), 5) for x in pos[n]]} for n, p in bone_hierarchy()]
