"""Landmarks 2D (README §5) y su correspondencia con la plantilla.

Cada landmark 2D corresponde al centroide de un conjunto de vértices de la plantilla. La fase 3
usa esta tabla para los residuos de landmarks del ajuste. Los tests la usan para medir el error
de la detección sobre siluetas sintéticas.
"""
from __future__ import annotations

import numpy as np

from .model import Template
from .parts_def import PART_INDEX

BODY_LANDMARKS = ["crown", "chin", "neck", "shoulder_l", "shoulder_r", "elbow_l", "elbow_r", "wrist_l", "wrist_r",
                  "fingertip_l", "fingertip_r", "crotch", "hip_l", "hip_r", "knee_l", "knee_r", "ankle_l", "ankle_r"]
FACE_LANDMARKS = ["eye_l", "eye_r", "nose", "mouth"]
LANDMARKS_2D = BODY_LANDMARKS + FACE_LANDMARKS

LABELS_ES = {
    "crown": "Coronilla", "chin": "Mentón", "neck": "Cuello", "crotch": "Entrepierna",
    "nose": "Nariz", "mouth": "Boca",
    **{f"{k}_{s}": f"{v} {'izq.' if s == 'l' else 'der.'}" for s in ("l", "r") for k, v in (
        ("shoulder", "Hombro"), ("elbow", "Codo"), ("wrist", "Muñeca"), ("fingertip", "Punta de dedos"),
        ("hip", "Cadera"), ("knee", "Rodilla"), ("ankle", "Tobillo"), ("eye", "Ojo"))},
}

BONES_2D = [("crown", "chin"), ("chin", "neck"), ("neck", "pelvis")] + [
    pair for s in ("l", "r") for pair in (
        ("neck", f"shoulder_{s}"), (f"shoulder_{s}", f"elbow_{s}"), (f"elbow_{s}", f"wrist_{s}"),
        (f"wrist_{s}", f"fingertip_{s}"), ("pelvis", f"hip_{s}"), (f"hip_{s}", f"knee_{s}"),
        (f"knee_{s}", f"ankle_{s}"))]


def derived_points(pts: dict) -> dict:
    """Puntos derivados que no se editan: la pelvis es el punto medio de las caderas."""
    if "hip_l" in pts and "hip_r" in pts:
        x, y = (np.asarray(pts["hip_l"], float) + np.asarray(pts["hip_r"], float)) / 2
        return {"pelvis": (float(x), float(y))}
    return {}


def template_correspondence(tpl: Template) -> dict[str, np.ndarray]:
    """Landmark 2D -> índices de vértice cuyo centroide lo define."""
    ring = {k: np.asarray(r["loop"]) for k, r in tpl.rings.items()}
    lm = tpl.landmarks
    top_parts = np.isin(tpl.part_id, [PART_INDEX["Head"], PART_INDEX["Hair"]])
    top_verts = np.unique(tpl.F[top_parts])
    out = {
        "crown": np.array([top_verts[np.argmax(tpl.V0[top_verts, 1])]]),
        "chin": np.array([lm["chin"]]),
        "neck": np.concatenate([ring["neck_top"], ring["neck_base"]]),
        "crotch": np.array([lm["crotch"]]),
        "nose": np.array([lm["nose_tip"]]),
        "mouth": ring["lips_inner"],
    }
    for s in ("l", "r"):
        out.update({
            f"shoulder_{s}": ring[f"upperarm_{s}"], f"elbow_{s}": ring[f"elbow_{s}"],
            f"wrist_{s}": ring[f"wrist_{s}"], f"fingertip_{s}": np.array([lm[f"fingertip_{s}"]]),
            f"hip_{s}": ring[f"thigh_{s}"], f"knee_{s}": ring[f"knee_{s}"], f"ankle_{s}": ring[f"ankle_{s}"],
            f"eye_{s}": np.asarray(tpl.loops[f"eye_center_{s}"]),
        })
    return out


def template_points(tpl: Template, V: np.ndarray | None = None) -> dict[str, np.ndarray]:
    V = tpl.V0 if V is None else V
    return {k: V[idx].mean(0) for k, idx in template_correspondence(tpl).items()}
