"""División en piezas, tapas internas y control de calidad (README §6.4 y §6.5)."""
from __future__ import annotations

from dataclasses import dataclass, field

import numpy as np

from .mesh.topology import mesh_stats, vertex_normals
from .template.model import Template
from .template.parts_def import PART_BY_ID, PARTS

CAP_INSET_CM = 0.4


@dataclass
class Part:
    id: str
    V: np.ndarray                  # (k,3) cm
    F: np.ndarray                  # (f,4) quads, índices locales
    uv: np.ndarray                 # (u,2)
    uv_faces: np.ndarray           # (f,4) índices en uv
    orig_index: np.ndarray         # (k,) vértice de la plantilla para cada vértice local
    rings: dict                    # nombre -> índices locales ordenados
    cap_V: np.ndarray = field(default_factory=lambda: np.zeros((0, 3)))
    cap_F: np.ndarray = field(default_factory=lambda: np.zeros((0, 3), np.int64))  # sobre [V | cap_V]
    groups: dict = field(default_factory=dict)  # nombre -> índices locales de vértice

    @property
    def centroid(self) -> np.ndarray:
        return self.V.mean(0)

    @property
    def definition(self):
        return PART_BY_ID[self.id]


def make_cap(V: np.ndarray, F: np.ndarray, ring: np.ndarray, inset: float, base: int):
    """Abanico de triángulos hundido `inset` cm hacia el interior de la pieza."""
    P = V[ring]
    c = P.mean(0)
    n = np.linalg.svd(P - c)[2][-1]
    ring_set = set(ring.tolist())
    adj = [f for f in F if ring_set.intersection(f.tolist())]
    inside = V[np.unique(np.asarray(adj))].mean(0) - c if adj else -n
    if n @ inside > 0:
        n = -n                      # n apunta hacia fuera de la pieza
    center = c - n * inset
    tris = []
    k = len(ring)
    for i in range(k):
        a, b = int(ring[i]), int(ring[(i + 1) % k])
        t = [a, b, base]
        if np.cross(V[b] - V[a], center - V[a]) @ n < 0:
            t = [b, a, base]
        tris.append(t)
    return center[None], np.asarray(tris, np.int64)


def split_parts(tpl: Template, V: np.ndarray | None = None, caps: bool = True) -> dict[str, Part]:
    V = tpl.V0 if V is None else V
    parts: dict[str, Part] = {}
    for pid, pdef in enumerate(PARTS):
        fmask = tpl.part_id == pid
        faces = tpl.F[fmask]
        used, local = np.unique(faces, return_inverse=True)  # duplica los vértices de anillo en cada pieza
        uvf = tpl.uv_faces[fmask]
        uv_used, uv_local = np.unique(uvf, return_inverse=True)
        P = Part(id=pdef.id, V=V[used].copy(), F=local.reshape(faces.shape), uv=tpl.uv[uv_used],
                 uv_faces=uv_local.reshape(uvf.shape), orig_index=used, rings={})
        for name, r in tpl.rings.items():
            if pdef.id in (r["a"], r["b"]):
                P.rings[name] = np.searchsorted(used, np.asarray(r["loop"]))
        for gname, gfaces in tpl.groups.get(pdef.id, {}).items():
            P.groups[gname] = np.searchsorted(used, np.unique(tpl.F[np.asarray(gfaces, np.int64)]))
        if caps and P.rings:
            cv, cf = [], []
            for ring in P.rings.values():
                c, t = make_cap(P.V, P.F, ring, CAP_INSET_CM, len(P.V) + len(cv))
                cv.append(c[0])
                cf.append(t)
            P.cap_V, P.cap_F = np.asarray(cv), np.concatenate(cf)
        parts[pdef.id] = P
    return parts


def explode_offsets(parts: dict[str, Part], factor: float = 1.0) -> dict[str, np.ndarray]:
    """Desplazamiento de cada pieza desde el centro de su bloque y del cuerpo (vista explosionada)."""
    body = np.mean([p.centroid for p in parts.values()], axis=0)
    out = {}
    for pid, p in parts.items():
        d = p.centroid - body
        d[1] *= 0.35
        out[pid] = d * factor
    return out


def quality_report(tpl: Template, parts: dict[str, Part], V: np.ndarray | None = None) -> dict:
    """Control de calidad de la fase 1: quads, manifold, anillos a 0,0 cm, simetría."""
    V = tpl.V0 if V is None else V
    per_part = {}
    for pid, p in parts.items():
        st = mesh_stats(p.V, p.F)
        st["rings"] = sorted(p.rings)
        per_part[pid] = st
    ring_err = {}
    for name, r in tpl.rings.items():
        if r["b"] is None:
            continue
        a, b = parts[r["a"]], parts[r["b"]]
        ring_err[name] = float(np.abs(a.V[a.rings[name]] - b.V[b.rings[name]]).max())
    sym = np.linalg.norm(V - V[tpl.mirror] * np.array([-1.0, 1.0, 1.0]), axis=1)
    whole = mesh_stats(V, tpl.F)
    return {
        "template": tpl.name,
        "parts": len(parts),
        "vertices": int(len(V)),
        "faces": int(len(tpl.F)),
        "quad_ratio": 1.0 if tpl.F.shape[1] == 4 else 0.0,
        "non_manifold_edges": whole["non_manifold_edges"],
        "flipped_edges": whole["flipped_edges"],
        "degenerate_faces": whole["degenerate_faces"],
        "ring_max_error_cm": max(ring_err.values()) if ring_err else 0.0,
        "rings": ring_err,
        "asymmetry_mean_cm": float(sym.mean()),
        "landmarks": len(tpl.landmarks),
        "morphs": len(tpl.morph_names),
        "bones": len(tpl.skeleton),
        "per_part": per_part,
    }


def part_normals(p: Part) -> np.ndarray:
    """Normales por pieza (las tapas no participan en el suavizado de la piel)."""
    return vertex_normals(p.V, p.F)
