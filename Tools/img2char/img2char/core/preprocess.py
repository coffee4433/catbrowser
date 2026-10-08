"""Carga y preproceso de imágenes (README §4).

Las imágenes se manejan en RGB uint8. Se conserva la original para las texturas (fase 6) y se
trabaja a un máximo de 2048 px de alto.
"""
from __future__ import annotations

from dataclasses import dataclass, field
from pathlib import Path

import cv2
import numpy as np
from PIL import Image, ImageOps

MAX_HEIGHT = 2048


@dataclass
class ImageInput:
    rgb: np.ndarray                     # (h,w,3) uint8, resolución de trabajo
    alpha: np.ndarray | None            # (h,w) uint8 si la imagen trae transparencia útil
    scale: float                        # trabajo = original · scale
    original_size: tuple[int, int]      # (w, h)
    path: str | None = None
    focal_35mm: float | None = None     # EXIF, para el aviso de perspectiva
    warnings: list = field(default_factory=list)

    @property
    def size(self) -> tuple[int, int]:
        return self.rgb.shape[1], self.rgb.shape[0]


def gray_world(rgb: np.ndarray) -> np.ndarray:
    """Balance de blancos gray-world: iguala la media de los tres canales."""
    f = rgb.reshape(-1, 3).astype(np.float64).mean(0)
    gain = f.mean() / np.maximum(f, 1e-6)
    return np.clip(rgb * gain, 0, 255).astype(np.uint8)


def load_image(src: str | Path | np.ndarray, max_height: int = MAX_HEIGHT, white_balance: bool = False) -> ImageInput:
    """Lee una imagen aplicando la orientación EXIF; separa el alfa si la imagen lo usa."""
    focal = None
    path = None
    if isinstance(src, np.ndarray):
        arr = src
    else:
        path = str(src)
        with Image.open(path) as im:
            exif = im.getexif()
            focal = exif.get_ifd(0x8769).get(0xA405) if exif else None  # FocalLengthIn35mmFilm
            im = ImageOps.exif_transpose(im)
            arr = np.asarray(im.convert("RGBA" if im.mode in ("RGBA", "LA", "P", "PA") else "RGB"))
    if arr.ndim == 2:
        arr = np.repeat(arr[:, :, None], 3, axis=2)
    rgb = np.ascontiguousarray(arr[:, :, :3])
    alpha = arr[:, :, 3] if arr.shape[2] == 4 else None
    if alpha is not None and (alpha.min() > 250 or (alpha > 127).mean() > 0.995):
        alpha = None  # alfa presente pero opaco: no aporta máscara
    h, w = rgb.shape[:2]
    scale = min(1.0, max_height / h)
    if scale < 1.0:
        size = (round(w * scale), round(h * scale))
        rgb = cv2.resize(rgb, size, interpolation=cv2.INTER_AREA)
        if alpha is not None:
            alpha = cv2.resize(alpha, size, interpolation=cv2.INTER_AREA)
    if white_balance:
        rgb = gray_world(rgb)
    out = ImageInput(rgb=rgb, alpha=alpha, scale=scale, original_size=(w, h), path=path,
                     focal_35mm=float(focal) if focal else None)
    if h < 1000:
        out.warnings.append(f"Resolución baja ({h} px de alto); se recomiendan ≥ 2000 px.")
    return out


def smooth_for_segmentation(rgb: np.ndarray) -> np.ndarray:
    """Filtro bilateral ligero, sólo para segmentar (nunca para la textura)."""
    return cv2.bilateralFilter(rgb, 7, 40, 7)
