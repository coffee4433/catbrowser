"""Subdivisión Catmull–Clark vectorizada para mallas de quads (con bordes)."""
from __future__ import annotations

import numpy as np


def unique_edges(F: np.ndarray, n: int):
    """Aristas no dirigidas de una malla de quads.

    Devuelve (edges (E,2) ordenadas, índice de arista por media-arista (m,4), caras por arista (E,)).
    """
    a = F.reshape(-1)
    b = np.roll(F, -1, axis=1).reshape(-1)
    lo, hi = np.minimum(a, b), np.maximum(a, b)
    keys = lo * n + hi
    uniq, inv, counts = np.unique(keys, return_inverse=True, return_counts=True)
    edges = np.stack([uniq // n, uniq % n], 1)
    return edges, inv.reshape(F.shape), counts


def edge_lookup(edges: np.ndarray, n: int):
    keys = edges[:, 0] * n + edges[:, 1]

    def look(a: int, b: int) -> int:
        k = min(a, b) * n + max(a, b)
        i = int(np.searchsorted(keys, k))
        if i >= len(keys) or keys[i] != k:
            raise KeyError((a, b))
        return i

    return look


def catmull_clark(V: np.ndarray, F: np.ndarray):
    """Un nivel de Catmull–Clark.

    Los vértices nuevos se ordenan [vértices originales | puntos de arista | puntos de cara], así
    los índices originales se conservan. La cara j produce las caras 4j..4j+3.
    """
    n, m = len(V), len(F)
    edges, eidx, counts = unique_edges(F, n)
    if (counts > 2).any():
        raise ValueError("malla no-manifold: arista con más de dos caras")
    E = len(edges)
    fp = V[F].mean(1)

    fsum = np.zeros((E, 3))
    np.add.at(fsum, eidx.reshape(-1), np.repeat(fp, 4, axis=0))
    boundary = counts == 1
    mid = V[edges].mean(1)
    ep = np.where(boundary[:, None], mid, (V[edges].sum(1) + fsum) / 4.0)

    valence = np.bincount(edges.reshape(-1), minlength=n).astype(float)
    nface = np.bincount(F.reshape(-1), minlength=n).astype(float)
    Q = np.zeros((n, 3))
    np.add.at(Q, F.reshape(-1), np.repeat(fp, 4, axis=0))
    R = np.zeros((n, 3))
    np.add.at(R, edges[:, 0], mid)
    np.add.at(R, edges[:, 1], mid)
    with np.errstate(invalid="ignore", divide="ignore"):
        Q /= nface[:, None]
        R /= valence[:, None]
        vp = (Q + 2 * R + (valence[:, None] - 3) * V) / valence[:, None]

    # Bordes: regla de curva B-spline (6P + vecinos) / 8 sobre las aristas de borde.
    be = edges[boundary]
    on_b = np.zeros(n, bool)
    on_b[be.reshape(-1)] = True
    nb_sum = np.zeros((n, 3))
    nb_cnt = np.zeros(n)
    np.add.at(nb_sum, be[:, 0], V[be[:, 1]])
    np.add.at(nb_sum, be[:, 1], V[be[:, 0]])
    np.add.at(nb_cnt, be.reshape(-1), 1)
    regular_b = on_b & (nb_cnt == 2)
    vp[regular_b] = (6 * V[regular_b] + nb_sum[regular_b]) / 8.0
    vp[on_b & ~regular_b] = V[on_b & ~regular_b]
    vp[valence == 0] = V[valence == 0]

    newV = np.concatenate([vp, ep, fp])
    fi = n + E + np.arange(m)
    newF = np.empty((m, 4, 4), np.int64)
    for i in range(4):
        newF[:, i] = np.stack([F[:, i], n + eidx[:, i], fi, n + eidx[:, i - 1]], 1)
    return newV, newF.reshape(-1, 4), edges
