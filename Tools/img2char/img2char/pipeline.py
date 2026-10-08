"""Orquestador del pipeline (README §1).

Cada etapa es una función pura sobre datos serializables. En la fase 1 sólo existen la plantilla,
los morphs manuales, la división en piezas y la exportación; el resto de etapas se declaran aquí
con la fase que las implementa para que la UI y la CLI muestren el estado real.
"""
from __future__ import annotations

from dataclasses import dataclass, field
from pathlib import Path

from .core.export import export_character
from .core.template.model import DEFAULT_TEMPLATE_DIR, Template, ensure_templates


@dataclass(frozen=True)
class Step:
    id: str
    title: str
    phase: int
    implemented: bool


STEPS = [
    Step("template", "Plantilla", 1, True),
    Step("input", "Entrada", 2, False),
    Step("mask", "Máscara", 2, False),
    Step("points", "Puntos", 2, False),
    Step("fit", "Ajuste", 3, False),
    Step("texture", "Textura", 6, False),
    Step("materials", "Materiales", 7, False),
    Step("rig", "Rig", 8, False),
    Step("export", "Exportar", 9, True),
]


@dataclass
class Job:
    template: str = "female_v1"
    images: dict = field(default_factory=dict)          # frontal*, lateral, trasera, cara, manos
    height_cm: float | None = None
    morphs: dict = field(default_factory=dict)          # valores manuales de morph (sliders de "Ajuste")
    manual_landmarks: dict = field(default_factory=dict)
    photogrammetry: bool = False
    caps: bool = True
    out_dir: Path = Path("out")
    template_dir: Path = DEFAULT_TEMPLATE_DIR


def load_template(name: str, root: Path | str = DEFAULT_TEMPLATE_DIR) -> Template:
    ensure_templates(root)
    return Template.load(Path(root) / name)


def build_character(job: Job) -> dict[str, Path]:
    if job.images or job.photogrammetry:
        raise NotImplementedError("La reconstrucción desde imágenes llega en las fases 2–4 (segmentación, "
                                  "landmarks y ajuste); por ahora sólo se exporta la plantilla con morphs.")
    tpl = load_template(job.template, job.template_dir)
    unknown = set(job.morphs) - set(tpl.morph_names)
    if unknown:
        raise ValueError(f"morphs desconocidos: {sorted(unknown)}")
    alpha = [float(job.morphs.get(n, 0.0)) for n in tpl.morph_names]
    return export_character(tpl, job.out_dir, alpha if any(alpha) else None, caps=job.caps)
