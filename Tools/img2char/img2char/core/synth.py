"""Render sintético de la plantilla (README §14: tests sin fotos reales).

Pinta la plantilla deformada con una cámara ortográfica (algoritmo del pintor, supermuestreo),
con ropa opcional de colores lisos. Devuelve la imagen y los mapas de verdad terreno: máscara,
pieza 3D por píxel y tipo de material por píxel.
"""
from __future__ import annotations

import cv2
import numpy as np

from .cameras import OrthoCamera
from .template.model import Template
from .template.parts_def import MATERIAL_COLORS, PARTS

KINDS = ["skin", "hair", "eye", "mouth", "garment", "footwear"]

OUTFIT = {  # pieza -> (tipo, color)
    **{p: ("garment", (0.18, 0.30, 0.58)) for p in ("Chest", "Abdomen", "Shoulder_L", "Shoulder_R",
                                                      "UpperArm_L", "UpperArm_R")},
    **{p: ("garment", (0.16, 0.16, 0.18)) for p in ("Hips", "Thigh_L", "Thigh_R", "Shin_L", "Shin_R")},
    **{p: ("footwear", (0.52, 0.14, 0.10)) for p in ("Foot_L", "Foot_R")},
}
IRIS = (0.22, 0.14, 0.08)
LIGHT = np.array([0.3, 0.6, 0.75]) / np.linalg.norm([0.3, 0.6, 0.75])


def _part_style(pid: int, outfit: bool) -> tuple[str, tuple]:
    p = PARTS[pid]
    if outfit and p.id in OUTFIT:
        return OUTFIT[p.id]
    kind = {"skin": "skin", "hair": "hair", "eye": "eye", "mouth": "mouth", "teeth": "mouth"}[p.material]
    return kind, MATERIAL_COLORS[p.material]


def camera_for(V: np.ndarray, height_px: int, margin: float = 0.06, view: str = "front") -> tuple[OrthoCamera, tuple]:
    """Cámara que encuadra el cuerpo con un margen; devuelve también el tamaño de imagen (w, h)."""
    H = V[:, 1].max() - V[:, 1].min()
    s = height_px * (1 - 2 * margin) / H
    cam = OrthoCamera(view=view, px_per_cm=s, cx=0.0, ground_v=height_px * (1 - margin) + V[:, 1].min() * s)
    u = (V @ cam.right) * s
    width = int(np.ceil((u.max() - u.min()) + 2 * margin * height_px))
    cam.cx = width / 2 - (u.max() + u.min()) / 2
    return cam, (width, height_px)


def render_view(tpl: Template, V: np.ndarray, cam: OrthoCamera, size: tuple[int, int], outfit: bool = True,
                bg=(236, 236, 232), supersample: int = 2) -> dict:
    w, h = size
    ss = supersample
    F, pid = tpl.F, tpl.part_id
    P = V[F]
    n = np.cross(P[:, 2] - P[:, 0], P[:, 3] - P[:, 1])
    n /= np.linalg.norm(n, axis=1, keepdims=True) + 1e-12
    visible = n @ cam.toward > 0
    order = np.flatnonzero(visible)[np.argsort(cam.depth(P[visible].mean(1)).ravel())]
    uv = cam.project(V) * ss
    pts = np.round(uv * 16).astype(np.int32)  # subpíxel (shift=4)

    styles = [_part_style(i, outfit) for i in range(len(PARTS))]
    shade = 0.42 + 0.58 * np.clip(n @ LIGHT, 0, 1)
    rgb = np.empty((h * ss, w * ss, 3), np.uint8)
    rgb[:] = bg
    part_map = np.full((h * ss, w * ss), 255, np.uint8)
    for fi in order:
        kind, col = styles[pid[fi]]
        if kind == "eye" and n[fi] @ cam.toward > 0.9:
            col = IRIS
        c = tuple(int(255 * min(1.0, x * shade[fi])) for x in col)
        poly = pts[F[fi]][None]
        cv2.fillPoly(rgb, poly, c, cv2.LINE_8, 4)
        cv2.fillPoly(part_map, poly, int(pid[fi]), cv2.LINE_8, 4)

    rgb = cv2.resize(rgb, (w, h), interpolation=cv2.INTER_AREA)
    cover = cv2.resize((part_map != 255).astype(np.float32), (w, h), interpolation=cv2.INTER_AREA)
    part_map = part_map[ss // 2::ss, ss // 2::ss][:h, :w]
    kind_lut = np.full(256, 255, np.uint8)
    for i in range(len(PARTS)):
        kind_lut[i] = KINDS.index(styles[i][0])
    return {"rgb": rgb, "alpha": (cover * 255).astype(np.uint8), "mask": (cover >= 0.5).astype(np.uint8) * 255,
            "part_map": part_map, "kind_map": kind_lut[part_map], "camera": cam}


def synth_photo(tpl: Template, alpha=None, height_px: int = 1600, outfit: bool = True, with_alpha: bool = False,
                view: str = "front") -> dict:
    """Foto sintética lista para el pipeline: RGB sobre fondo liso (o RGBA si with_alpha)."""
    V = tpl.deformed(alpha)
    cam, size = camera_for(V, height_px, view=view)
    out = render_view(tpl, V, cam, size, outfit=outfit)
    out["image"] = np.dstack([out["rgb"], out["alpha"]]) if with_alpha else out["rgb"]
    out["V"] = V
    out["height_cm"] = float(V[:, 1].max() - V[:, 1].min())
    return out
