"""Imágenes de superposición para la UI y la CLI (RGB uint8)."""
from __future__ import annotations

import cv2
import numpy as np

from .template.landmarks2d import BONES_2D

ACCENT = (108, 140, 255)
PART_COLORS = np.array([
    (239, 187, 92), (108, 140, 255), (76, 195, 138), (46, 160, 110), (255, 140, 120), (220, 100, 90),
    (180, 120, 255), (140, 90, 220), (255, 214, 102), (230, 180, 60)], np.uint8)
KIND_COLORS = {"skin": (241, 170, 140), "hair": (120, 80, 50), "footwear": (200, 70, 60), "accessory": (250, 220, 90)}
GARMENT_COLORS = [(108, 140, 255), (76, 195, 138), (180, 120, 255), (90, 200, 220), (255, 160, 80)]


def kind_color(kind: str) -> tuple:
    base, _, n = kind.partition("_")
    if base == "garment":
        return GARMENT_COLORS[(int(n or 1) - 1) % len(GARMENT_COLORS)]
    return KIND_COLORS.get(base, (200, 200, 200))


def _blend(rgb: np.ndarray, color_img: np.ndarray, sel: np.ndarray, alpha: float) -> np.ndarray:
    out = rgb.astype(np.float32)
    out[sel] = out[sel] * (1 - alpha) + color_img[sel].astype(np.float32) * alpha
    return out.astype(np.uint8)


def mask_overlay(rgb: np.ndarray, mask: np.ndarray, alpha: float = 0.45) -> np.ndarray:
    """Fondo oscurecido, silueta teñida con el acento y contorno marcado."""
    out = (rgb.astype(np.float32) * 0.35).astype(np.uint8)
    out[mask > 0] = rgb[mask > 0]
    tint = np.zeros_like(rgb)
    tint[:] = ACCENT
    out = _blend(out, tint, mask > 0, alpha * 0.5)
    contours, _ = cv2.findContours(mask, cv2.RETR_CCOMP, cv2.CHAIN_APPROX_NONE)
    cv2.drawContours(out, contours, -1, ACCENT, max(1, rgb.shape[0] // 700), cv2.LINE_AA)
    return out


def materials_image(a, rgb: np.ndarray | None = None, alpha: float = 0.65) -> np.ndarray:
    seg = a.seg
    base = (a.image.rgb if rgb is None else rgb).copy()
    lut = np.array([kind_color(k) for k in seg.material_kinds] + [(0, 0, 0)], np.uint8)
    col = lut[np.where(seg.materials >= 0, seg.materials, len(lut) - 1)]
    out = _blend((base * 0.35).astype(np.uint8), col, np.zeros(seg.mask.shape, bool), 0)
    out[seg.mask > 0] = base[seg.mask > 0]
    return _blend(out, col, seg.materials >= 0, alpha)


def parts_image(a, alpha: float = 0.6) -> np.ndarray:
    seg = a.seg
    base = a.image.rgb
    col = PART_COLORS[np.clip(seg.parts2d, 0, len(PART_COLORS) - 1)]
    out = (base * 0.35).astype(np.uint8)
    out[seg.mask > 0] = base[seg.mask > 0]
    out = _blend(out, col, seg.parts2d >= 0, alpha)
    edges = cv2.Canny((seg.parts2d + 1).astype(np.uint8) * 20, 1, 1) > 0
    out[edges] = (255, 255, 255)
    return out


def landmarks_overlay(a) -> np.ndarray:
    out = mask_overlay(a.image.rgb, a.seg.mask, 0.25)
    pts = a.landmarks.all_points()
    t = max(2, a.image.rgb.shape[0] // 500)
    for p, q in BONES_2D:
        cv2.line(out, tuple(int(v) for v in pts[p]), tuple(int(v) for v in pts[q]), (255, 255, 255), t, cv2.LINE_AA)
    for name, (x, y) in a.landmarks.points.items():
        c = a.landmarks.confidence.get(name, 1.0)
        color = (76, 195, 138) if c >= 0.5 else (255, 107, 107)
        cv2.circle(out, (int(x), int(y)), 3 * t, color, -1, cv2.LINE_AA)
        cv2.circle(out, (int(x), int(y)), 3 * t, (14, 16, 19), 1, cv2.LINE_AA)
    return out
