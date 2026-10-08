"""Exportación glTF 2.0 binaria (.glb): un nodo/malla por pieza + primitiva de tapas (README §11).

Fase 1: geometría, normales, UV y materiales planos. Skin, texturas PBR y morph targets se añaden
en las fases 7–9.
"""
from __future__ import annotations

from pathlib import Path

import numpy as np
from pygltflib import (ARRAY_BUFFER, ELEMENT_ARRAY_BUFFER, FLOAT, GLTF2, SCALAR, UNSIGNED_INT, VEC2, VEC3,
                       Accessor, Asset, Attributes, Buffer, BufferView, Material, Mesh, Node,
                       PbrMetallicRoughness, Primitive, Scene)

from ..parts import Part, part_normals
from ..template.parts_def import MATERIAL_COLORS

CM_TO_M = 0.01


def part_render_arrays(p: Part):
    """Vértices expandidos por (vértice, uv) y triángulos de una pieza, en cm."""
    corners = np.stack([p.F.reshape(-1), p.uv_faces.reshape(-1)], 1)
    keys, inv = np.unique(corners, axis=0, return_inverse=True)
    N = part_normals(p)
    pos, nrm, uv = p.V[keys[:, 0]], N[keys[:, 0]], p.uv[keys[:, 1]]
    q = inv.reshape(-1, 4)
    tris = np.concatenate([q[:, [0, 1, 2]], q[:, [0, 2, 3]]])
    return pos, nrm, uv, tris


def cap_render_arrays(p: Part):
    """Triángulos de las tapas sin indexar, con normales planas."""
    if not len(p.cap_F):
        return None
    allV = np.concatenate([p.V, p.cap_V])
    T = allV[p.cap_F]
    n = np.cross(T[:, 1] - T[:, 0], T[:, 2] - T[:, 0])
    n /= np.linalg.norm(n, axis=1, keepdims=True) + 1e-12
    pos = T.reshape(-1, 3)
    return pos, np.repeat(n, 3, axis=0), np.arange(len(pos)).reshape(-1, 3)


class _Builder:
    def __init__(self):
        self.g = GLTF2(asset=Asset(generator="img2char", version="2.0"))
        self.blob = bytearray()

    def accessor(self, arr: np.ndarray, typ: str, target: int, minmax: bool = False) -> int:
        arr = np.ascontiguousarray(arr)
        comp = UNSIGNED_INT if arr.dtype == np.uint32 else FLOAT
        while len(self.blob) % 4:
            self.blob.append(0)
        self.g.bufferViews.append(BufferView(buffer=0, byteOffset=len(self.blob), byteLength=arr.nbytes,
                                             target=target))
        self.blob.extend(arr.tobytes())
        acc = Accessor(bufferView=len(self.g.bufferViews) - 1, componentType=comp,
                       count=len(arr) if typ != SCALAR else arr.size, type=typ)
        if minmax:
            acc.min, acc.max = arr.min(0).tolist(), arr.max(0).tolist()
        self.g.accessors.append(acc)
        return len(self.g.accessors) - 1

    def primitive(self, pos, nrm, tris, material: int, uv=None) -> Primitive:
        attrs = Attributes(POSITION=self.accessor((pos * CM_TO_M).astype(np.float32), VEC3, ARRAY_BUFFER, True),
                           NORMAL=self.accessor(nrm.astype(np.float32), VEC3, ARRAY_BUFFER))
        if uv is not None:
            attrs.TEXCOORD_0 = self.accessor(np.stack([uv[:, 0], 1 - uv[:, 1]], 1).astype(np.float32), VEC2,
                                             ARRAY_BUFFER)
        idx = self.accessor(tris.reshape(-1).astype(np.uint32), SCALAR, ELEMENT_ARRAY_BUFFER)
        return Primitive(attributes=attrs, indices=idx, material=material)


def write_glb(parts: dict[str, Part], path: Path | str, caps: bool = True) -> Path:
    b = _Builder()
    mats = {}
    for name, rgb in MATERIAL_COLORS.items():
        mats[name] = len(b.g.materials)
        b.g.materials.append(Material(name=name, doubleSided=False, pbrMetallicRoughness=PbrMetallicRoughness(
            baseColorFactor=[*rgb, 1.0], metallicFactor=0.0, roughnessFactor=0.9 if name == "cap" else 0.6)))
    children = []
    for pid, p in parts.items():
        pos, nrm, uv, tris = part_render_arrays(p)
        prims = [b.primitive(pos, nrm, tris, mats[p.definition.material], uv)]
        cap = cap_render_arrays(p) if caps else None
        if cap is not None:
            prims.append(b.primitive(*cap, mats["cap"]))
        b.g.meshes.append(Mesh(name=pid, primitives=prims))
        b.g.nodes.append(Node(name=pid, mesh=len(b.g.meshes) - 1,
                              extras={"block": p.definition.block, "bone": p.definition.bone,
                                      "rings": sorted(p.rings)}))
        children.append(len(b.g.nodes) - 1)
    b.g.nodes.append(Node(name="Character", children=children))
    b.g.scenes = [Scene(nodes=[len(b.g.nodes) - 1])]
    b.g.scene = 0
    b.g.buffers = [Buffer(byteLength=len(b.blob))]
    b.g.set_binary_blob(bytes(b.blob))
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    b.g.save_binary(str(path))
    return path
