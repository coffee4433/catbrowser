"""Etapas 4–5 del pipeline sobre la vista frontal: preproceso → máscara → landmarks → partes 2D →
materiales → cámara. Cada paso se puede rehacer por separado tras una corrección manual en la UI.
"""
from __future__ import annotations

import json
from dataclasses import dataclass
from pathlib import Path

import cv2
import numpy as np

from .cameras import OrthoCamera, fit_camera
from .landmarks import Landmarks2D, detect_landmarks
from .preprocess import ImageInput, load_image
from .regions import (MATERIAL_KINDS, PARTS_2D, classify_materials, cluster_rgb, kind_base, kind_label,
                      material_clusters, parts_2d)
from .segment import Segmentation, internal_edges, silhouette_mask


@dataclass
class Analysis:
    image: ImageInput
    seg: Segmentation
    landmarks: Landmarks2D
    height_cm: float
    camera: OrthoCamera | None = None
    k: int = 8

    @property
    def warnings(self) -> list[str]:
        return [*self.image.warnings, *self.landmarks.warnings]

    # ---------------------------------------------------------------- etapas
    def redo_landmarks(self) -> None:
        """Tras editar la máscara: re-detecta (conserva los puntos movidos a mano)."""
        manual = {k: self.landmarks.points[k] for k in self.landmarks.manual}
        self.landmarks = detect_landmarks(self.seg.mask, self.image.rgb)
        for k, p in manual.items():
            self.landmarks.move(k, *p)
        self.redo_regions()

    def redo_regions(self) -> None:
        """Tras mover puntos o editar la máscara: partes 2D, materiales y cámara."""
        rgb, seg = self.image.rgb, self.seg
        seg.edges = internal_edges(rgb, seg.mask)
        seg.parts2d = parts_2d(rgb, seg.mask, self.landmarks, seg.edges)
        labels, centers = material_clusters(rgb, seg.mask, self.k)
        labels, centers, kinds = classify_materials(rgb, labels, centers, seg.parts2d, self.landmarks)
        seg.materials = labels
        seg.material_rgb = cluster_rgb(rgb, labels, len(kinds))
        seg.material_kinds = kinds
        self.redo_camera()

    def redo_camera(self) -> None:
        lm = self.landmarks
        pelvis = lm.all_points().get("pelvis", lm.points["crotch"])
        self.camera = fit_camera(lm.height_px, lm.bottom, pelvis[0], self.height_cm)

    def set_material_kind(self, cluster: int, kind: str) -> None:
        if kind_base(kind) not in MATERIAL_KINDS:
            raise ValueError(f"tipo de material desconocido: {kind}")
        self.seg.material_kinds[cluster] = kind

    # ------------------------------------------------------------- resumen
    def materials_summary(self) -> list[dict]:
        seg = self.seg
        total = max(1, int((seg.materials >= 0).sum()))
        return [{"id": c, "kind": kind, "label": kind_label(kind), "rgb": seg.material_rgb[c].tolist(),
                 "share": round(float((seg.materials == c).sum() / total), 4)}
                for c, kind in enumerate(seg.material_kinds)]

    def to_dict(self) -> dict:
        return {"image": self.image.path, "size": list(self.image.size), "scale": self.image.scale,
                "height_cm": self.height_cm, "camera": self.camera.to_dict() if self.camera else None,
                "landmarks": self.landmarks.to_dict(), "materials": self.materials_summary(),
                "parts2d": PARTS_2D, "warnings": self.warnings}

    def save(self, out_dir: Path | str) -> dict[str, Path]:
        from .overlays import landmarks_overlay, materials_image, parts_image

        out = Path(out_dir)
        out.mkdir(parents=True, exist_ok=True)
        files = {"mask": out / "mask.png", "materials": out / "materials.png", "parts2d": out / "parts2d.png",
                 "overlay": out / "overlay.png", "analysis": out / "analysis.json"}
        cv2.imwrite(str(files["mask"]), self.seg.mask)
        cv2.imwrite(str(files["materials"]), cv2.cvtColor(materials_image(self), cv2.COLOR_RGB2BGR))
        cv2.imwrite(str(files["parts2d"]), cv2.cvtColor(parts_image(self), cv2.COLOR_RGB2BGR))
        cv2.imwrite(str(files["overlay"]), cv2.cvtColor(landmarks_overlay(self), cv2.COLOR_RGB2BGR))
        files["analysis"].write_text(json.dumps(self.to_dict(), indent=1, ensure_ascii=False), encoding="utf-8")
        return files


def analyze(src, height_cm: float, rect=None, white_balance: bool = False, k: int = 8,
            manual_landmarks: dict | None = None) -> Analysis:
    img = load_image(src, white_balance=white_balance)
    mask = silhouette_mask(img.rgb, img.alpha, rect)
    if not mask.any():
        raise ValueError("No se ha encontrado ninguna silueta; ajusta el rectángulo o usa un fondo liso.")
    lm = detect_landmarks(mask, img.rgb)
    for name, p in (manual_landmarks or {}).items():
        lm.move(name, *p)
    a = Analysis(image=img, seg=Segmentation(mask=mask, rect=rect), landmarks=lm, height_cm=height_cm, k=k)
    a.redo_regions()
    return a


def mask_from_array(mask: np.ndarray) -> np.ndarray:
    return np.where(mask > 127, 255, 0).astype(np.uint8)
