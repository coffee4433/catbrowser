"""Anillos de unión: fronteras entre partes de la malla única de la plantilla (README §2)."""
from __future__ import annotations

import numpy as np

from ..mesh.topology import boundary_loops
from .parts_def import HOLE_RINGS, PARTS, RING_NAMES


def find_rings(V: np.ndarray, F: np.ndarray, part_id: np.ndarray) -> dict[str, dict]:
    """Devuelve {nombre: {"a": parte, "b": parte | None, "loop": [vértices ordenados]}}."""
    owner = {}
    for fi, f in enumerate(F):
        for i in range(4):
            owner[(int(f[i]), int(f[(i + 1) % 4]))] = int(part_id[fi])
    rings: dict[str, dict] = {}
    for pid, pdef in enumerate(PARTS):
        for loop in boundary_loops(F[part_id == pid]):
            partners = {owner.get((b, a)) for a, b in zip(loop, loop[1:] + loop[:1])}
            if len(partners) != 1:
                raise ValueError(f"anillo de {pdef.id} toca varias partes: {partners}")
            other = partners.pop()
            if other is None:
                side = "l" if V[loop, 0].mean() > 0 else "r"
                name = f"{HOLE_RINGS[pdef.id]}_{side}"
                b = None
            else:
                b = PARTS[other].id
                name = RING_NAMES[frozenset((pdef.id, b))]
            if name not in rings:
                rings[name] = {"a": pdef.id, "b": b, "loop": [int(v) for v in loop]}
    return rings
