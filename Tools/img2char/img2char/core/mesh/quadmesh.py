"""Constructor de mallas de quads (box modeling) para la plantilla procedural.

Todas las operaciones conservan una malla 100 % de quads con orientación coherente
(normales hacia fuera). Cada cara lleva una etiqueta de parte; los anillos de unión
salen después de las fronteras entre etiquetas.
"""
from __future__ import annotations

from dataclasses import dataclass, field
from typing import Callable, Iterable, Sequence

import numpy as np

from .subdivide import catmull_clark, edge_lookup


def unit(v) -> np.ndarray:
    v = np.asarray(v, float)
    n = np.linalg.norm(v)
    return v / n if n > 0 else v


def rotation_between(a, b) -> np.ndarray:
    """Matriz de rotación mínima que lleva la dirección a sobre b."""
    a, b = unit(a), unit(b)
    v = np.cross(a, b)
    c = float(a @ b)
    if c < -1 + 1e-9:
        p = np.cross(a, [1.0, 0.0, 0.0])
        if np.linalg.norm(p) < 1e-6:
            p = np.cross(a, [0.0, 1.0, 0.0])
        p = unit(p)
        return 2 * np.outer(p, p) - np.eye(3)
    k = np.array([[0, -v[2], v[1]], [v[2], 0, -v[0]], [-v[1], v[0], 0]])
    return np.eye(3) + k + k @ k / (1 + c)


def frame(axis, ref) -> tuple[np.ndarray, np.ndarray, np.ndarray]:
    """Base ortonormal (a, u, v) con a = eje y u lo más parecido posible a ref."""
    a = unit(axis)
    ref = np.asarray(ref, float)
    u = unit(ref - a * (a @ ref))
    return a, u, np.cross(a, u)


def plane_normal(P: np.ndarray, hint=None) -> np.ndarray:
    n = np.linalg.svd(P - P.mean(0))[2][-1]
    if hint is not None and n @ np.asarray(hint, float) < 0:
        n = -n
    return n


def ring_transform(center, axis, hu, hv, ref=(0.0, 0.0, 1.0), n_old=None) -> Callable:
    """Transformación para extruir: gira el anillo hacia `axis` y lo reescala a (hu, hv)."""
    center = np.asarray(center, float)

    def fn(P: np.ndarray) -> np.ndarray:
        c0 = P.mean(0)
        n0 = n_old if n_old is not None else plane_normal(P, axis)
        r = (P - c0) @ rotation_between(n0, axis).T
        _, u, v = frame(axis, ref)
        ru, rv = r @ u, r @ v
        su = hu / max(np.abs(ru).max(), 1e-9)
        sv = hv / max(np.abs(rv).max(), 1e-9)
        return center + np.outer(ru * su, u) + np.outer(rv * sv, v)

    return fn


def scale_transform(scale=1.0, offset=(0.0, 0.0, 0.0)) -> Callable:
    scale = np.broadcast_to(np.asarray(scale, float), (3,))
    offset = np.asarray(offset, float)

    def fn(P: np.ndarray) -> np.ndarray:
        c0 = P.mean(0)
        return c0 + (P - c0) * scale + offset

    return fn


def order_loops(edges: Iterable[tuple[int, int]]) -> list[list[int]]:
    """Ordena aristas dirigidas a→b en bucles cerrados."""
    nxt: dict[int, int] = {}
    for a, b in edges:
        if a in nxt:
            raise ValueError(f"vértice {a} con dos aristas de borde salientes")
        nxt[a] = b
    loops, seen = [], set()
    for start in sorted(nxt):
        if start in seen:
            continue
        loop, v = [], start
        while v not in seen:
            seen.add(v)
            loop.append(v)
            v = nxt[v]
        if v != start:
            raise ValueError("borde abierto")
        loops.append(loop)
    return loops


@dataclass
class ExtrudeResult:
    sides: list[int]
    loop_old: list[int]
    loop_new: list[int]
    mapping: dict


@dataclass
class QuadMesh:
    verts: list = field(default_factory=list)
    F: list = field(default_factory=list)           # list[list[int] | None]; None = borrada
    label: list = field(default_factory=list)
    groups: dict = field(default_factory=dict)      # nombre -> set(cara)
    loops: dict = field(default_factory=dict)       # nombre -> [vértices] cíclico

    # ------------------------------------------------------------------ básicos
    @property
    def V(self) -> np.ndarray:
        return np.asarray(self.verts, float).reshape(-1, 3)

    def add_vertex(self, p) -> int:
        self.verts.append(np.asarray(p, float))
        return len(self.verts) - 1

    def add_face(self, vs: Sequence[int], label: str, outward=None) -> int:
        vs = list(vs)
        if outward is not None and self._newell(vs) @ np.asarray(outward, float) < 0:
            vs = vs[::-1]
        self.F.append(vs)
        self.label.append(label)
        return len(self.F) - 1

    def _newell(self, vs) -> np.ndarray:
        P = np.asarray([self.verts[v] for v in vs])
        return np.cross(P - P.mean(0), np.roll(P, -1, 0) - P.mean(0)).sum(0)

    def alive(self) -> list[int]:
        return [i for i, f in enumerate(self.F) if f is not None]

    def faces_labeled(self, *labels: str) -> list[int]:
        return [i for i in self.alive() if self.label[i] in labels]

    def face_center(self, fi: int) -> np.ndarray:
        return np.mean([self.verts[v] for v in self.F[fi]], axis=0)

    def face_normal(self, fi: int) -> np.ndarray:
        return unit(self._newell(self.F[fi]))

    def delete_faces(self, fis: Iterable[int]) -> None:
        for f in fis:
            self.F[f] = None
            for g in self.groups.values():
                g.discard(f)

    def add_to_group(self, name: str, fis: Iterable[int]) -> None:
        self.groups.setdefault(name, set()).update(fis)

    # ---------------------------------------------------------------- operaciones
    def extrude(self, faces: Sequence[int], transform: Callable | dict, label: str | None = None,
                side_label: str | None = None) -> ExtrudeResult:
        """Extruye una región de caras: duplica sus vértices, mueve la región y cose lados.

        `transform` es una función P(k,3) -> P'(k,3) o un dict vértice -> posición nueva.
        """
        faces = list(faces)
        verts = sorted({v for f in faces for v in self.F[f]})
        if isinstance(transform, dict):
            Q = [transform[v] for v in verts]
        else:
            Q = transform(np.asarray([self.verts[v] for v in verts]))
        new = {v: self.add_vertex(q) for v, q in zip(verts, Q)}
        directed = {}
        for f in faces:
            fv = self.F[f]
            for i in range(4):
                directed[(fv[i], fv[(i + 1) % 4])] = f
        boundary = [(a, b) for (a, b) in directed if (b, a) not in directed]
        sides = [self.add_face([a, b, new[b], new[a]], side_label or label or self.label[directed[(a, b)]])
                 for a, b in boundary]
        for f in faces:
            self.F[f] = [new[v] for v in self.F[f]]
            if label is not None:
                self.label[f] = label
        loops = order_loops(boundary)
        if len(loops) != 1:
            raise ValueError("la región a extruir debe tener un solo borde")
        return ExtrudeResult(sides, loops[0], [new[v] for v in loops[0]], new)

    def loft(self, faces: Sequence[int], rings: Sequence[dict], label: str, ref=(0.0, 0.0, 1.0)) -> list[ExtrudeResult]:
        """Extrusiones sucesivas; cada anillo es dict(center, axis, hu, hv)."""
        out = []
        for r in rings:
            out.append(self.extrude(faces, ring_transform(r["center"], r["axis"], r["hu"], r["hv"], r.get("ref", ref)),
                                    label=label))
        return out

    def bridge(self, face: int, target_loop: Sequence[int], label: str) -> list[int]:
        """Une el contorno de `face` con un agujero existente (`target_loop`) y borra la cara."""
        src = self.F[face]
        if len(target_loop) != 4:
            raise ValueError("bridge sólo admite agujeros de 4 vértices")
        Ps = np.asarray([self.verts[v] for v in src])
        Pt = np.asarray([self.verts[v] for v in target_loop])
        Ps_c, Pt_c = Ps - Ps.mean(0), Pt - Pt.mean(0)
        best, match = None, None
        for d in (1, -1):
            t = list(target_loop)[::d]
            for k in range(4):
                cand = t[k:] + t[:k]
                cost = sum(np.sum((Ps_c[i] - Pt_c[list(target_loop).index(cand[i])]) ** 2) for i in range(4))
                if best is None or cost < best:
                    best, match = cost, cand
        m = dict(zip(src, match))
        sides = [self.add_face([a, b, m[b], m[a]], label) for a, b in zip(src, src[1:] + src[:1])]
        self.delete_faces([face])
        return sides

    def box_grid(self, dims: tuple[int, int, int], pos: Callable[[float, float, float], np.ndarray],
                 label_fn: Callable[[str, int, int], str | None]) -> dict:
        """Superficie de una caja subdividida (nx, ny, nz) deformada por `pos(i, j, k)`.

        `label_fn(lado, a, b)` devuelve la etiqueta de la celda o None para dejar un agujero.
        Devuelve el mapa (i, j, k) -> vértice.
        """
        n = dims
        ids: dict = {}

        def vid(c):
            if c not in ids:
                ids[c] = self.add_vertex(pos(*c))
            return ids[c]

        for axis in range(3):
            o1, o2 = [a for a in range(3) if a != axis]
            for side, val in (("-", 0), ("+", n[axis])):
                name = side + "xyz"[axis]
                normal = np.zeros(3)
                normal[axis] = 1 if side == "+" else -1
                for a in range(n[o1]):
                    for b in range(n[o2]):
                        lab = label_fn(name, a, b)
                        if lab is None:
                            continue
                        corners = []
                        for da, db in ((0, 0), (1, 0), (1, 1), (0, 1)):
                            c = [0, 0, 0]
                            c[axis], c[o1], c[o2] = val, a + da, b + db
                            corners.append(tuple(c))
                        g = np.asarray(corners, float)
                        if np.cross(g[1] - g[0], g[3] - g[0]) @ normal < 0:
                            corners = corners[::-1]
                        self.F.append([vid(c) for c in corners])
                        self.label.append(lab)
        return ids

    def shell(self, faces: Sequence[int], inner: float, outer: float, label: str) -> list[int]:
        """Copia una región como sólido con grosor (casco), desplazada por las normales."""
        faces = list(faces)
        verts = sorted({v for f in faces for v in self.F[f]})
        N = {v: np.zeros(3) for v in verts}
        for f in faces:
            n = self._newell(self.F[f])
            for v in self.F[f]:
                N[v] += n
        vin = {v: self.add_vertex(self.verts[v] + unit(N[v]) * inner) for v in verts}
        vout = {v: self.add_vertex(self.verts[v] + unit(N[v]) * outer) for v in verts}
        new, directed = [], set()
        for f in faces:
            fv = self.F[f]
            new.append(self.add_face([vout[v] for v in fv], label))
            new.append(self.add_face([vin[v] for v in fv[::-1]], label))
            directed.update((fv[i], fv[(i + 1) % 4]) for i in range(4))
        for a, b in directed:
            if (b, a) not in directed:
                new.append(self.add_face([vout[b], vout[a], vin[a], vin[b]], label))
        return new

    # --------------------------------------------------------------- selección
    def nearest_face(self, point, candidates: Sequence[int]) -> int:
        C = np.asarray([self.face_center(f) for f in candidates])
        return candidates[int(np.argmin(np.linalg.norm(C - np.asarray(point, float), axis=1)))]

    def _edge_faces(self) -> dict:
        ef: dict = {}
        for f in self.alive():
            fv = self.F[f]
            for i in range(4):
                ef.setdefault(frozenset((fv[i], fv[(i + 1) % 4])), []).append(f)
        return ef

    def walk(self, face: int, direction, ef: dict | None = None, allowed: set | None = None) -> int:
        """Cara vecina a través de la arista que mejor apunta hacia `direction` (dentro de `allowed`)."""
        ef = ef or self._edge_faces()
        fv, c = self.F[face], self.face_center(face)
        best, out = -np.inf, face
        for i in range(4):
            a, b = fv[i], fv[(i + 1) % 4]
            d = unit((self.verts[a] + self.verts[b]) / 2 - c) @ unit(direction)
            if d > best:
                nb = [g for g in ef[frozenset((a, b))] if g != face and (allowed is None or g in allowed)]
                if nb:
                    best, out = d, nb[0]
        return out

    def select_rect(self, seed_point, u_dir, v_dir, nu: int, nv: int, candidates: Sequence[int]) -> list[int]:
        """Rectángulo de nu×nv caras de la rejilla, centrado en la cara más cercana a seed_point."""
        ef = self._edge_faces()
        seed = self.nearest_face(seed_point, candidates)
        allowed = set(candidates)

        def line(start, d, n):
            back = [start]
            for _ in range((n - 1) // 2):
                back.append(self.walk(back[-1], -np.asarray(d, float), ef, allowed))
            fwd = [start]
            for _ in range(n // 2):
                fwd.append(self.walk(fwd[-1], d, ef, allowed))
            return back[::-1] + fwd[1:]

        out = []
        for f in line(seed, u_dir, nu):
            out.extend(line(f, v_dir, nv))
        if len(set(out)) != nu * nv:
            raise ValueError("selección rectangular degenerada")
        return out

    # ------------------------------------------------------------ subdivisión
    def subdivided(self) -> "QuadMesh":
        """Catmull–Clark de un nivel; conserva etiquetas, grupos y bucles con nombre."""
        alive = self.alive()
        remap = {f: j for j, f in enumerate(alive)}
        F = np.asarray([self.F[f] for f in alive], np.int64)
        V2, F2, edges = catmull_clark(self.V, F)
        n = len(self.verts)
        out = QuadMesh(verts=list(V2), F=[list(f) for f in F2],
                       label=[lab for f in alive for lab in [self.label[f]] * 4])
        for name, g in self.groups.items():
            out.groups[name] = {4 * remap[f] + k for f in g for k in range(4)}
        look = edge_lookup(edges, n)
        for name, loop in self.loops.items():
            new = []
            for a, b in zip(loop, loop[1:] + loop[:1]):
                new += [a, n + look(a, b)]
            out.loops[name] = new
        return out

    def compact(self) -> tuple[np.ndarray, np.ndarray, list[str], dict, dict]:
        """Arrays finales (V, F, etiquetas, grupos, bucles) sin caras borradas."""
        alive = self.alive()
        remap = {f: j for j, f in enumerate(alive)}
        groups = {k: sorted(remap[f] for f in g if f in remap) for k, g in self.groups.items()}
        return (self.V, np.asarray([self.F[f] for f in alive], np.int64),
                [self.label[f] for f in alive], groups, {k: [int(x) for x in v] for k, v in self.loops.items()})
