"""Formato de la plantilla en disco (README §3) y deformación por morphs."""
from __future__ import annotations

import json
from dataclasses import asdict, dataclass, field
from pathlib import Path

import numpy as np

from .parts_def import PARTS

DEFAULT_TEMPLATE_DIR = Path(__file__).resolve().parents[3] / "templates"
TEMPLATE_NAMES = ("male_v1", "female_v1")


def _dump(path: Path, data) -> None:
    path.write_text(json.dumps(data, indent=1, ensure_ascii=False), encoding="utf-8")


def _load(path: Path):
    return json.loads(path.read_text(encoding="utf-8"))


@dataclass
class Template:
    name: str
    V0: np.ndarray                 # (n,3) cm, Y arriba, frente +Z, izquierda del personaje +X
    F: np.ndarray                  # (m,4) quads
    part_id: np.ndarray            # (m,) índice en PARTS
    uv: np.ndarray                 # (k,2)
    uv_faces: np.ndarray           # (m,4) índices en uv
    mirror: np.ndarray             # (n,) vértice simétrico respecto a X
    landmarks: dict                # nombre -> vértice
    rings: dict                    # nombre -> {"a", "b", "loop"}
    loops: dict                    # bucles auxiliares (falanges, ojos, bola del pie…)
    groups: dict                   # parte -> grupo -> [caras]
    skeleton: list                 # [{"name", "parent", "head"}]
    morph_names: list
    morph_T: np.ndarray            # (K,n,3) desplazamientos por unidad de morph
    morph_info: list               # [{"name", "lo", "hi", "unit"}]
    meta: dict = field(default_factory=dict)

    @property
    def part_names(self) -> list[str]:
        return [p.id for p in PARTS]

    @property
    def lo(self) -> np.ndarray:
        return np.array([m["lo"] for m in self.morph_info])

    @property
    def hi(self) -> np.ndarray:
        return np.array([m["hi"] for m in self.morph_info])

    def deformed(self, alpha=None) -> np.ndarray:
        if alpha is None:
            return self.V0.copy()
        return self.V0 + np.tensordot(np.asarray(alpha, float), self.morph_T, axes=1)

    # ------------------------------------------------------------------ disco
    def save(self, root: Path | str) -> Path:
        d = Path(root) / self.name
        (d / "morphs").mkdir(parents=True, exist_ok=True)
        np.savez_compressed(d / "mesh.npz", V0=self.V0.astype(np.float32), F=self.F.astype(np.int32),
                            part_id=self.part_id.astype(np.int16), uv=self.uv.astype(np.float32),
                            uv_faces=self.uv_faces.astype(np.int32), mirror=self.mirror.astype(np.int32))
        for name, T in zip(self.morph_names, self.morph_T):
            np.save(d / "morphs" / f"{name}.npy", T.astype(np.float32))
        _dump(d / "morphs.json", self.morph_info)
        _dump(d / "landmarks.json", self.landmarks)
        _dump(d / "rings.json", self.rings)
        _dump(d / "loops.json", self.loops)
        _dump(d / "groups.json", self.groups)
        _dump(d / "skeleton.json", self.skeleton)
        _dump(d / "parts.json", [asdict(p) for p in PARTS])
        _dump(d / "meta.json", self.meta)
        return d

    @classmethod
    def load(cls, path: Path | str) -> "Template":
        d = Path(path)
        z = np.load(d / "mesh.npz")
        info = _load(d / "morphs.json")
        names = [m["name"] for m in info]
        T = np.stack([np.load(d / "morphs" / f"{n}.npy").astype(np.float64) for n in names]) if names \
            else np.zeros((0, len(z["V0"]), 3))
        parts = _load(d / "parts.json")
        if [p["id"] for p in parts] != [p.id for p in PARTS]:
            raise ValueError(f"{d}: la tabla de partes no coincide con la versión del programa")
        return cls(name=d.name, V0=z["V0"].astype(np.float64), F=z["F"].astype(np.int64),
                   part_id=z["part_id"].astype(np.int64), uv=z["uv"].astype(np.float64),
                   uv_faces=z["uv_faces"].astype(np.int64), mirror=z["mirror"].astype(np.int64),
                   landmarks=_load(d / "landmarks.json"), rings=_load(d / "rings.json"),
                   loops=_load(d / "loops.json"), groups=_load(d / "groups.json"),
                   skeleton=_load(d / "skeleton.json"), morph_names=names, morph_T=T, morph_info=info,
                   meta=_load(d / "meta.json"))


def available_templates(root: Path | str = DEFAULT_TEMPLATE_DIR) -> list[str]:
    root = Path(root)
    return sorted(p.name for p in root.iterdir() if (p / "mesh.npz").exists()) if root.exists() else []


def ensure_templates(root: Path | str = DEFAULT_TEMPLATE_DIR) -> list[str]:
    """Genera las plantillas procedurales si todavía no existen."""
    names = available_templates(root)
    if not all(n in names for n in TEMPLATE_NAMES):
        from .build import build_templates

        for t in build_templates().values():
            t.save(root)
        names = available_templates(root)
    return names
