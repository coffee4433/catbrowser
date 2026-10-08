"""Segmentación de la silueta (README §4): GrabCut + refinado de bordes con Sobel + Canny.

Nada de redes: GrabCut es un corte de grafos sobre mezclas de gaussianas.
"""
from __future__ import annotations

from dataclasses import dataclass, field

import cv2
import numpy as np


@dataclass
class Segmentation:
    mask: np.ndarray                         # (h,w) uint8 0/255
    rect: tuple | None = None                # rectángulo de GrabCut (x, y, w, h) a resolución de trabajo
    edges: np.ndarray | None = None          # Canny (bordes internos)
    parts2d: np.ndarray | None = None        # (h,w) int8, -1 fondo, índice en regions.PARTS_2D
    materials: np.ndarray | None = None      # (h,w) int16, -1 fondo, índice de cluster
    material_rgb: np.ndarray | None = None   # (k,3) uint8, color medio de cada cluster
    material_kinds: list = field(default_factory=list)  # tipo por cluster: skin, hair, garment_1…, footwear

    def dist_to_boundary(self) -> np.ndarray:
        """Distancia euclídea (sin signo) al borde de la máscara."""
        inside = cv2.distanceTransform(self.mask, cv2.DIST_L2, 5)
        outside = cv2.distanceTransform(255 - self.mask, cv2.DIST_L2, 5)
        return np.where(self.mask > 0, inside, outside)

    def dist_outside(self) -> np.ndarray:
        """Distancia a la silueta para píxeles de fondo; cero dentro."""
        return cv2.distanceTransform(255 - self.mask, cv2.DIST_L2, 5)


def default_rect(w: int, h: int) -> tuple[int, int, int, int]:
    return int(w * 0.05), int(h * 0.02), int(w * 0.90), int(h * 0.96)


def clean_mask(fg: np.ndarray, min_hole_frac: float = 0.0002) -> np.ndarray:
    """Cierre/apertura, componente mayor y relleno de motas.

    Los agujeros reales (hueco entre brazo y torso, entre los muslos) se conservan.
    """
    fg = cv2.morphologyEx(fg, cv2.MORPH_CLOSE, np.ones((5, 5), np.uint8))
    fg = cv2.morphologyEx(fg, cv2.MORPH_OPEN, np.ones((3, 3), np.uint8))
    n, lab, stats, _ = cv2.connectedComponentsWithStats(fg, connectivity=8)
    if n <= 1:
        return np.zeros_like(fg)
    keep = 1 + int(np.argmax(stats[1:, cv2.CC_STAT_AREA]))
    out = np.where(lab == keep, 255, 0).astype(np.uint8)
    n, lab, stats, _ = cv2.connectedComponentsWithStats(255 - out, connectivity=4)
    area = out.size * min_hole_frac
    for i in range(1, n):
        x, y, w, h, a = stats[i]
        touches = x == 0 or y == 0 or x + w == out.shape[1] or y + h == out.shape[0]
        if not touches and a < area:
            out[lab == i] = 255
    return out


def grabcut_mask(rgb: np.ndarray, rect=None, init_mask: np.ndarray | None = None, iters: int = 5,
                 work_height: int | None = None) -> np.ndarray:
    """GrabCut a resolución reducida; devuelve una máscara 0/255 a la resolución de entrada."""
    h, w = rgb.shape[:2]
    work_height = work_height or int(np.clip(h / 2.5, 800, 1024))
    s = min(1.0, work_height / h)
    small = cv2.resize(rgb, (round(w * s), round(h * s)), interpolation=cv2.INTER_AREA) if s < 1 else rgb
    bgr = cv2.cvtColor(small, cv2.COLOR_RGB2BGR)
    gc = np.zeros(small.shape[:2], np.uint8)
    bgd, fgd = np.zeros((1, 65), np.float64), np.zeros((1, 65), np.float64)
    if init_mask is not None:
        gc[:] = cv2.resize(init_mask, (small.shape[1], small.shape[0]), interpolation=cv2.INTER_NEAREST)
        cv2.grabCut(bgr, gc, None, bgd, fgd, iters, cv2.GC_INIT_WITH_MASK)
    else:
        rect = rect or default_rect(w, h)
        r = tuple(int(round(v * s)) for v in rect)
        cv2.grabCut(bgr, gc, r, bgd, fgd, iters, cv2.GC_INIT_WITH_RECT)
    fg = np.where((gc == cv2.GC_FGD) | (gc == cv2.GC_PR_FGD), 255, 0).astype(np.uint8)
    if s < 1:
        fg = cv2.resize(cv2.GaussianBlur(fg, (3, 3), 0), (w, h), interpolation=cv2.INTER_LINEAR)
        fg = np.where(fg >= 128, 255, 0).astype(np.uint8)
    return fg


def refine_edges(mask: np.ndarray, rgb: np.ndarray, radius: int = 4, min_contrast: float = 12.0) -> np.ndarray:
    """Desplaza cada punto del contorno, a lo largo de su normal, al máximo de |Sobel| a ±radius px."""
    gray = cv2.cvtColor(rgb, cv2.COLOR_RGB2GRAY).astype(np.float32)
    gray = cv2.GaussianBlur(gray, (3, 3), 0)
    mag = cv2.magnitude(cv2.Sobel(gray, cv2.CV_32F, 1, 0, ksize=3), cv2.Sobel(gray, cv2.CV_32F, 0, 1, ksize=3))
    h, w = mask.shape
    contours, hier = cv2.findContours(mask, cv2.RETR_CCOMP, cv2.CHAIN_APPROX_NONE)
    if hier is None:
        return mask
    offs = np.arange(-radius, radius + 1, dtype=np.float32)
    refined = []
    for c in contours:
        pts = c[:, 0, :].astype(np.float32)
        if len(pts) < 12:
            refined.append(c)
            continue
        t = np.roll(pts, -3, 0) - np.roll(pts, 3, 0)
        nrm = np.stack([t[:, 1], -t[:, 0]], 1)
        nrm /= np.linalg.norm(nrm, axis=1, keepdims=True) + 1e-6
        samp = pts[:, None, :] + offs[None, :, None] * nrm[:, None, :]
        xs = np.clip(np.round(samp[..., 0]).astype(int), 0, w - 1)
        ys = np.clip(np.round(samp[..., 1]).astype(int), 0, h - 1)
        vals = mag[ys, xs]
        best = offs[np.argmax(vals, 1)]
        best[vals.max(1) - vals.min(1) < min_contrast] = 0.0
        k = 5  # mediana circular: evita dientes de sierra
        pad = np.concatenate([best[-k:], best, best[:k]])
        best = np.median(np.lib.stride_tricks.sliding_window_view(pad, 2 * k + 1), axis=1)
        refined.append(np.round(pts + best[:, None] * nrm).astype(np.int32)[:, None, :])
    out = np.zeros_like(mask)
    outer = [refined[i] for i in range(len(refined)) if hier[0][i][3] < 0]
    holes = [refined[i] for i in range(len(refined)) if hier[0][i][3] >= 0]
    cv2.drawContours(out, outer, -1, 255, cv2.FILLED)
    cv2.drawContours(out, holes, -1, 0, cv2.FILLED)
    return out


def silhouette_mask(rgb: np.ndarray, alpha: np.ndarray | None = None, rect=None) -> np.ndarray:
    if alpha is not None:  # render con alfa (skin PNG): la máscara ya existe
        return clean_mask(np.where(alpha > 127, 255, 0).astype(np.uint8))
    fg = clean_mask(grabcut_mask(rgb, rect))
    radius = max(2, int(round(4 * rgb.shape[0] / 2048)) + 1)
    return clean_mask(refine_edges(fg, rgb, radius))


def internal_edges(rgb: np.ndarray, mask: np.ndarray) -> np.ndarray:
    """Canny sobre la imagen suavizada, limitado al interior de la silueta."""
    gray = cv2.GaussianBlur(cv2.cvtColor(rgb, cv2.COLOR_RGB2GRAY), (5, 5), 0)
    e = cv2.Canny(gray, 50, 150)
    return cv2.bitwise_and(e, cv2.erode(mask, np.ones((5, 5), np.uint8)))


def paint(mask: np.ndarray, p0, p1, radius: float, add: bool) -> None:
    """Pincel de añadir/quitar: trazo de p0 a p1 (coordenadas de imagen), en el sitio."""
    cv2.line(mask, tuple(int(round(v)) for v in p0), tuple(int(round(v)) for v in p1), 255 if add else 0,
             max(1, int(round(2 * radius))), cv2.LINE_8)
    cv2.circle(mask, tuple(int(round(v)) for v in p1), max(1, int(round(radius))), 255 if add else 0, -1)


def iou(a: np.ndarray, b: np.ndarray) -> float:
    a, b = a > 0, b > 0
    u = np.logical_or(a, b).sum()
    return float(np.logical_and(a, b).sum() / u) if u else 1.0
