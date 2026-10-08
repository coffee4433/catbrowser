"""Cámaras ortográficas por vista (README §5, modo A).

Convención de mundo: cm, Y arriba, frente +Z, izquierda del personaje +X. En la vista frontal la
izquierda del personaje sale a la derecha de la imagen.
"""
from __future__ import annotations

from dataclasses import asdict, dataclass

import numpy as np

# Eje del mundo que apunta a la derecha de la imagen y eje de profundidad (hacia la cámara).
VIEW_AXES = {
    "front": (np.array([1.0, 0.0, 0.0]), np.array([0.0, 0.0, 1.0])),
    "back": (np.array([-1.0, 0.0, 0.0]), np.array([0.0, 0.0, -1.0])),
    "left": (np.array([0.0, 0.0, -1.0]), np.array([1.0, 0.0, 0.0])),     # cámara en el lado izquierdo
    "right": (np.array([0.0, 0.0, 1.0]), np.array([-1.0, 0.0, 0.0])),
}


@dataclass
class OrthoCamera:
    view: str
    px_per_cm: float
    cx: float          # columna de la imagen donde cae el eje vertical del personaje
    ground_v: float    # fila de la imagen donde está el suelo (y = 0)

    @property
    def right(self) -> np.ndarray:
        return VIEW_AXES[self.view][0]

    @property
    def toward(self) -> np.ndarray:
        return VIEW_AXES[self.view][1]

    def project(self, P: np.ndarray) -> np.ndarray:
        P = np.atleast_2d(P)
        return np.stack([self.cx + (P @ self.right) * self.px_per_cm, self.ground_v - P[:, 1] * self.px_per_cm], 1)

    def depth(self, P: np.ndarray) -> np.ndarray:
        """Mayor = más cerca de la cámara."""
        return np.atleast_2d(P) @ self.toward

    def to_dict(self) -> dict:
        return asdict(self)


def fit_camera(height_px: float, sole_v: float, center_u: float, height_cm: float, view: str = "front") -> OrthoCamera:
    """Escala desde la altura conocida: px_por_cm = altura_px / altura_cm."""
    if height_cm <= 0 or height_px <= 0:
        raise ValueError("altura no válida")
    return OrthoCamera(view=view, px_per_cm=height_px / height_cm, cx=center_u, ground_v=sole_v)
