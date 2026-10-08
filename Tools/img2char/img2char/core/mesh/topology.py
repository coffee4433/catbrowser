"""Utilidades de topología y control de calidad de mallas."""
from __future__ import annotations

import numpy as np

from .quadmesh import order_loops


def boundary_edges(F: np.ndarray) -> list[tuple[int, int]]:
    """Aristas dirigidas de borde de un conjunto de caras (polígonos de igual tamaño)."""
    k = F.shape[1]
    directed = {(int(f[i]), int(f[(i + 1) % k])) for f in F for i in range(k)}
    return [(a, b) for a, b in directed if (b, a) not in directed]


def boundary_loops(F: np.ndarray) -> list[list[int]]:
    return order_loops(boundary_edges(F)) if len(F) else []


def face_normals(V: np.ndarray, F: np.ndarray) -> np.ndarray:
    """Normales de Newell (no normalizadas, módulo = 2·área para triángulos)."""
    P = V[F]
    c = P.mean(1, keepdims=True)
    return np.cross(P - c, np.roll(P, -1, axis=1) - c).sum(1)


def vertex_normals(V: np.ndarray, F: np.ndarray) -> np.ndarray:
    fn = face_normals(V, F)
    N = np.zeros_like(V)
    for i in range(F.shape[1]):
        np.add.at(N, F[:, i], fn)
    norm = np.linalg.norm(N, axis=1, keepdims=True)
    return np.divide(N, norm, out=np.zeros_like(N), where=norm > 0)


def mesh_stats(V: np.ndarray, F: np.ndarray) -> dict:
    """Estadísticas de calidad: aristas no-manifold, orientación, caras degeneradas, bordes."""
    k = F.shape[1]
    a = F.reshape(-1)
    b = np.roll(F, -1, axis=1).reshape(-1)
    n = max(len(V), 1)
    und, counts = np.unique(np.minimum(a, b) * n + np.maximum(a, b), return_counts=True)
    _, dcounts = np.unique(a * n + b, return_counts=True)
    area = np.linalg.norm(face_normals(V, F), axis=1) / 2
    return {
        "vertices": int(len(np.unique(F))),
        "faces": int(len(F)),
        "quads": float(1.0 if k == 4 else 0.0),
        "non_manifold_edges": int((counts > 2).sum()),
        "flipped_edges": int((dcounts > 1).sum()),
        "boundary_edges": int((counts == 1).sum()),
        "degenerate_faces": int((area < 1e-6).sum()),
        "edges": int(len(und)),
    }
