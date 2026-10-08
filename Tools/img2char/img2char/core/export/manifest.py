"""manifest.json (README §2) y exportación completa de una plantilla/personaje."""
from __future__ import annotations

import json
from pathlib import Path

import numpy as np

from ..parts import Part, quality_report, split_parts
from ..template.model import Template
from .gltf import write_glb


def build_manifest(tpl: Template, parts: dict[str, Part]) -> dict:
    out_parts = []
    for pid, p in parts.items():
        d = p.definition
        rings = {}
        for name in sorted(p.rings):
            r = tpl.rings[name]
            rings[name] = r["b"] if r["a"] == pid else r["a"]
        out_parts.append({"id": pid, "block": d.block, "bone": d.bone, "material": d.material,
                          "rings": rings, "groups": list(d.groups), "blendshapes": list(d.blendshapes)})
    seams = []
    for name, r in tpl.rings.items():
        if r["b"] is None:
            continue
        a, b = parts[r["a"]], parts[r["b"]]
        seams.append({"ring": name, "a": r["a"], "b": r["b"],
                      "vertices_a": a.rings[name].tolist(), "vertices_b": b.rings[name].tolist()})
    return {"template": tpl.name, "units": "cm", "up_axis": "Y", "mirror_axis": "X",
            "export_units": "m", "parts": out_parts, "seams": seams, "merge": []}


def export_character(tpl: Template, out_dir: Path | str, alpha=None, caps: bool = True) -> dict[str, Path]:
    """Escribe character.glb, manifest.json y report.json en out_dir."""
    out = Path(out_dir)
    out.mkdir(parents=True, exist_ok=True)
    V = tpl.deformed(alpha)
    parts = split_parts(tpl, V, caps=caps)
    glb = write_glb(parts, out / "character.glb", caps=caps)
    (out / "manifest.json").write_text(json.dumps(build_manifest(tpl, parts), indent=1), encoding="utf-8")
    report = quality_report(tpl, parts, V)
    if alpha is not None:
        report["morphs_applied"] = dict(zip(tpl.morph_names, np.asarray(alpha, float).round(4).tolist()))
    (out / "report.json").write_text(json.dumps(report, indent=1), encoding="utf-8")
    return {"glb": glb, "manifest": out / "manifest.json", "report": out / "report.json"}
