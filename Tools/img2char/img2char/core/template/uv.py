"""UV provisionales de la plantilla (README §7): proyección por parte + empaquetado por material.

Cilíndrica alrededor del eje de la parte (costura en la cara oculta) o plana. Es determinista y
suficiente para heredar UV; LSCM/ABF++ para piezas regeneradas llega en la fase 6.
"""
from __future__ import annotations

import numpy as np

from .parts_def import PARTS


def _project(P: np.ndarray, method: str, seam_dir: np.ndarray) -> tuple[np.ndarray, float | None]:
    """Coordenadas 2D en cm por vértice y, si es cilíndrica, el periodo en u (perímetro medio)."""
    c = P.mean(0)
    Q = P - c
    if method == "planar_z":
        return Q[:, [0, 1]], None
    if method == "planar_x":
        sign = 1.0 if c[0] > 0 else -1.0  # espejo para que la oreja derecha no salga invertida
        return np.stack([-Q[:, 2] * sign, Q[:, 1]], 1), None
    if method == "cyl_y":
        axis = np.array([0.0, 1.0, 0.0])
    elif method == "cyl_z":
        axis = np.array([0.0, 0.0, 1.0])
    else:
        axis = np.linalg.svd(Q, full_matrices=False)[2][0]
        if axis @ np.array([0.0, -1.0, 0.1]) < 0:
            axis = -axis
    e1 = seam_dir - axis * (seam_dir @ axis)
    if np.linalg.norm(e1) < 1e-6:
        e1 = np.cross(axis, [1.0, 0.0, 0.0])
    e1 /= np.linalg.norm(e1)
    e2 = np.cross(axis, e1)
    h = Q @ axis
    period = 2 * np.pi * np.linalg.norm(Q - np.outer(h, axis), axis=1).mean()
    theta = np.arctan2(Q @ e2, -(Q @ e1))  # la costura (±π) queda en la dirección seam_dir
    return np.stack([(theta + np.pi) / (2 * np.pi) * period, h], 1), period


def unwrap(V: np.ndarray, F: np.ndarray, part_id: np.ndarray, padding: float = 0.01):
    """UV por esquina de cara: devuelve (uv (k,2) en [0,1], uv_faces (m,4))."""
    corner_uv = np.zeros((len(F), 4, 2))
    islands = []
    for pid, pdef in enumerate(PARTS):
        fidx = np.flatnonzero(part_id == pid)
        if not len(fidx):
            continue
        verts = np.unique(F[fidx])
        P = V[verts]
        seam = np.array([0.0, 0.0, -1.0]) if pdef.block != "arms" else np.array([0.0, -1.0, 0.0])
        uv2, period = _project(P, pdef.uv, seam)
        lookup = np.full(len(V), -1)
        lookup[verts] = np.arange(len(verts))
        cu = uv2[lookup[F[fidx]]]
        if period:
            span = cu[:, :, 0].max(1) - cu[:, :, 0].min(1)
            wrap = span > period / 2
            cu[wrap, :, 0] = np.where(cu[wrap, :, 0] < period / 2, cu[wrap, :, 0] + period, cu[wrap, :, 0])
        corner_uv[fidx] = cu
        islands.append((pid, fidx))

    # Empaquetado por estantes, un atlas por material, misma densidad de texel dentro del atlas.
    by_mat: dict[str, list] = {}
    for pid, fidx in islands:
        by_mat.setdefault(PARTS[pid].material, []).append(fidx)
    for fl in by_mat.values():
        boxes = []
        for fidx in fl:
            cu = corner_uv[fidx].reshape(-1, 2)
            lo, hi = cu.min(0), cu.max(0)
            corner_uv[fidx] -= lo
            boxes.append((fidx, hi - lo))
        total = sum(w * h for _, (w, h) in boxes)
        width = max(np.sqrt(total) * 1.15, max(w for _, (w, _) in boxes))
        pad = width * padding
        x = y = row_h = 0.0
        placed = []
        for fidx, (w, h) in sorted(boxes, key=lambda b: -b[1][1]):
            if x + w > width:
                x, y, row_h = 0.0, y + row_h + pad, 0.0
            placed.append((fidx, x, y))
            x += w + pad
            row_h = max(row_h, h)
        size = max(width, y + row_h)
        for fidx, ox, oy in placed:
            corner_uv[fidx] = (corner_uv[fidx] + [ox, oy]) / size

    flat = corner_uv.reshape(-1, 2)
    keys = np.concatenate([F.reshape(-1, 1), np.round(flat * 1e6)], 1)
    uniq, inv = np.unique(keys, axis=0, return_inverse=True)
    uv = uniq[:, 1:] / 1e6
    return uv, inv.reshape(F.shape)
