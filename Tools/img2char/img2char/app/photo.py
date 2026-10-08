"""Controlador de las pantallas de la fase 2 (Entrada, Máscara, Puntos).

El trabajo pesado (GrabCut, partes 2D, k-means) se ejecuta en un hilo; la UI sólo ve propiedades
y una revisión de imagen que invalida la caché del proveedor de imágenes.
"""
from __future__ import annotations

import tempfile
import threading
from pathlib import Path

import numpy as np
from PySide6.QtCore import Property, QObject, QSize, QUrl, Signal, Slot
from PySide6.QtGui import QImage
from PySide6.QtQuick import QQuickImageProvider

from ..core.analyze import Analysis, analyze
from ..core.landmarks import detect_landmarks
from ..core.overlays import kind_color, landmarks_overlay, mask_overlay, materials_image, parts_image
from ..core.regions import MATERIAL_ES, MATERIAL_KINDS, PARTS_2D, PARTS_2D_ES
from ..core.segment import clean_mask, paint, silhouette_mask
from ..core.template.landmarks2d import BONES_2D, LABELS_ES, LANDMARKS_2D

SLOTS = [  # id, título, obligatoria, fase en la que se usa
    ("front", "Frontal", True, 2),
    ("side", "Lateral", False, 4),
    ("back", "Trasera", False, 4),
    ("face", "Rostro frontal", False, 4),
]


def to_qimage(rgb: np.ndarray) -> QImage:
    rgb = np.ascontiguousarray(rgb)
    h, w = rgb.shape[:2]
    return QImage(rgb.data, w, h, 3 * w, QImage.Format.Format_RGB888).copy()


class OverlayProvider(QQuickImageProvider):
    """image://overlay/<modo>/<revisión> con modo en photo|mask|materials|parts|points."""

    def __init__(self, ctrl: "PhotoController"):
        super().__init__(QQuickImageProvider.ImageType.Image)
        self.ctrl = ctrl

    def requestImage(self, id_: str, size: QSize, requested: QSize) -> QImage:
        img = self.ctrl.render(id_.split("/")[0])
        if img is None:
            img = QImage(1, 1, QImage.Format.Format_RGB888)
            img.fill(0)
        if size is not None:
            size.setWidth(img.width())
            size.setHeight(img.height())
        return img


class PhotoController(QObject):
    changed = Signal()
    revisionChanged = Signal()
    busyChanged = Signal()
    message = Signal(str)
    _done = Signal(object, str)

    def __init__(self):
        super().__init__()
        self._images: dict[str, str] = {}
        self._height = 175.0
        self._white_balance = False
        self._a: Analysis | None = None
        self._busy = False
        self._busy_text = ""
        self._rev = 0
        self._selected_material = -1
        self._done.connect(self._on_done)

    # ------------------------------------------------------------ propiedades
    @Property("QVariantList", notify=changed)
    def slots(self):
        return [{"id": i, "title": t, "required": r, "phase": ph, "path": self._images.get(i, ""),
                 "url": QUrl.fromLocalFile(self._images[i]).toString() if i in self._images else ""}
                for i, t, r, ph in SLOTS]

    @Property(float, notify=changed)
    def heightCm(self):
        return self._height

    @Property(bool, notify=changed)
    def whiteBalance(self):
        return self._white_balance

    @Property(bool, notify=changed)
    def canAnalyze(self):
        return "front" in self._images and self._height > 0 and not self._busy

    @Property(bool, notify=changed)
    def hasAnalysis(self):
        return self._a is not None

    @Property(bool, notify=busyChanged)
    def busy(self):
        return self._busy

    @Property(str, notify=busyChanged)
    def busyText(self):
        return self._busy_text

    @Property(int, notify=revisionChanged)
    def revision(self):
        return self._rev

    @Property(int, notify=changed)
    def imageWidth(self):
        return self._a.image.size[0] if self._a else 0

    @Property(int, notify=changed)
    def imageHeight(self):
        return self._a.image.size[1] if self._a else 0

    @Property("QVariantList", notify=changed)
    def warnings(self):
        return self._a.warnings if self._a else []

    @Property("QVariantMap", notify=changed)
    def stats(self):
        if not self._a:
            return {}
        lm, cam = self._a.landmarks, self._a.camera
        return {"heights": round(float(lm.heads), 2), "headPx": float(lm.head_height), "top": float(lm.top),
                "bottom": float(lm.bottom), "pxPerCm": round(float(cam.px_per_cm), 3) if cam else 0,
                "heightPx": float(lm.height_px),
                "maskPx": int((self._a.seg.mask > 0).sum())}

    @Property("QVariantList", notify=changed)
    def landmarks(self):
        if not self._a:
            return []
        lm = self._a.landmarks
        return [{"name": k, "label": LABELS_ES[k], "x": float(lm.points[k][0]), "y": float(lm.points[k][1]),
                 "confidence": float(lm.confidence.get(k, 1.0)), "manual": k in lm.manual} for k in LANDMARKS_2D]

    @Property("QVariantList", notify=changed)
    def bones(self):
        if not self._a:
            return []
        P = self._a.landmarks.all_points()
        return [[float(P[a][0]), float(P[a][1]), float(P[b][0]), float(P[b][1])] for a, b in BONES_2D]

    @Property("QVariantList", notify=changed)
    def materials(self):
        if not self._a:
            return []
        out = []
        for m in self._a.materials_summary():
            m["swatch"] = "#%02x%02x%02x" % tuple(m["rgb"])
            m["kindColor"] = "#%02x%02x%02x" % kind_color(m["kind"])
            out.append(m)
        return sorted(out, key=lambda m: -m["share"])

    @Property("QVariantList", constant=True)
    def materialKinds(self):
        out = []
        for k in MATERIAL_KINDS:
            if k == "garment":
                out += [{"id": f"garment_{i}", "label": f"prenda {i}"} for i in range(1, 6)]
            else:
                out.append({"id": k, "label": MATERIAL_ES[k]})
        return out

    @Property("QVariantList", constant=True)
    def partsLegend(self):
        from ..core.overlays import PART_COLORS

        return [{"id": p, "label": PARTS_2D_ES[p], "color": "#%02x%02x%02x" % tuple(int(c) for c in PART_COLORS[i])}
                for i, p in enumerate(PARTS_2D)]

    @Property(int, notify=changed)
    def selectedMaterial(self):
        return self._selected_material

    # ------------------------------------------------------------------ render
    def render(self, mode: str) -> QImage | None:
        a = self._a
        if a is None:
            if mode == "photo" and "front" in self._images:
                return QImage(self._images["front"])
            return None
        if mode == "photo":
            return to_qimage(a.image.rgb)
        if mode == "mask":
            return to_qimage(mask_overlay(a.image.rgb, a.seg.mask))
        if mode == "materials":
            img = materials_image(a)
            if self._selected_material >= 0:
                sel = a.seg.materials == self._selected_material
                img = img.copy()
                img[~sel & (a.seg.mask > 0)] = (img[~sel & (a.seg.mask > 0)] * 0.35).astype(np.uint8)
            return to_qimage(img)
        if mode == "parts":
            return to_qimage(parts_image(a))
        if mode == "points":
            return to_qimage(mask_overlay(a.image.rgb, a.seg.mask, 0.15))
        if mode == "landmarks":
            return to_qimage(landmarks_overlay(a))
        return None

    def _bump(self) -> None:
        self._rev += 1
        self.revisionChanged.emit()

    # --------------------------------------------------------------- trabajo
    def _run(self, text: str, fn) -> None:
        if self._busy:
            return
        self._busy, self._busy_text = True, text
        self.busyChanged.emit()
        self.changed.emit()

        def work():
            try:
                self._done.emit(fn(), "")
            except Exception as e:  # el error se muestra en la barra de estado
                self._done.emit(None, str(e))

        threading.Thread(target=work, daemon=True).start()

    @Slot(object, str)
    def _on_done(self, result, error: str) -> None:
        self._busy, self._busy_text = False, ""
        if isinstance(result, Analysis):
            self._a = result
        if error:
            self.message.emit(f"Error: {error}")
        elif self._a is not None:
            n = len(self._a.warnings)
            self.message.emit("Análisis listo" + (f" · {n} aviso(s)" if n else " · sin avisos"))
        self.busyChanged.emit()
        self.changed.emit()
        self._bump()

    # ----------------------------------------------------------------- entrada
    @Slot(str, QUrl)
    def setImage(self, slot: str, url: QUrl) -> None:
        path = url.toLocalFile() if url.isLocalFile() else url.toString()
        if not Path(path).is_file():
            self.message.emit(f"No se encuentra {path}")
            return
        self._images[slot] = path
        if slot == "front":
            self._a = None
        self.changed.emit()
        self._bump()

    @Slot(str)
    def clearImage(self, slot: str) -> None:
        self._images.pop(slot, None)
        if slot == "front":
            self._a = None
        self.changed.emit()
        self._bump()

    @Slot(float)
    def setHeightCm(self, v: float) -> None:
        self._height = float(v)
        if self._a:
            self._a.height_cm = self._height
            self._a.redo_camera()
        self.changed.emit()

    @Slot(bool)
    def setWhiteBalance(self, v: bool) -> None:
        self._white_balance = bool(v)
        self.changed.emit()

    @Slot(str)
    def useSample(self, template: str) -> None:
        """Foto de prueba renderizada desde la plantilla (sin fotos reales a mano)."""
        import cv2

        from ..core.synth import synth_photo
        from ..pipeline import load_template

        t = load_template(template)
        rng = np.random.default_rng()
        alpha = np.array([0.0 if m["name"] == "gender" else rng.uniform(max(m["lo"], -0.6), min(m["hi"], 0.6))
                          for m in t.morph_info])
        r = synth_photo(t, alpha)
        path = Path(tempfile.gettempdir()) / f"img2char_muestra_{template}.png"
        cv2.imwrite(str(path), cv2.cvtColor(r["rgb"], cv2.COLOR_RGB2BGR))
        self._height = round(r["height_cm"], 1)
        self.setImage("front", QUrl.fromLocalFile(str(path)))

    @Slot()
    def analyze(self) -> None:
        if "front" not in self._images:
            return
        src, h, wb = self._images["front"], self._height, self._white_balance
        self._run("Segmentando y buscando puntos…", lambda: analyze(src, h, white_balance=wb))

    # ----------------------------------------------------------------- máscara
    @Slot(float, float, float, float, float, bool)
    def paintStroke(self, x0, y0, x1, y1, radius, add) -> None:
        if not self._a or self._busy:
            return
        paint(self._a.seg.mask, (x0, y0), (x1, y1), radius, add)
        self._bump()

    @Slot()
    def commitMask(self) -> None:
        """Tras el pincel: limpia la máscara y rehace puntos (conservando los manuales) y regiones."""
        a = self._a
        if not a:
            return

        def work():
            a.seg.mask = clean_mask(a.seg.mask)
            a.redo_landmarks()
            return a

        self._run("Recalculando puntos, partes y materiales…", work)

    @Slot(float, float, float, float)
    def setRect(self, x, y, w, h) -> None:
        """Nuevo rectángulo de GrabCut: recalcula la máscara entera."""
        a = self._a
        if not a or w < 10 or h < 10:
            return
        rect = (int(x), int(y), int(w), int(h))

        def work():
            a.seg.rect = rect
            a.seg.mask = silhouette_mask(a.image.rgb, a.image.alpha, rect)
            a.landmarks = detect_landmarks(a.seg.mask, a.image.rgb)
            a.redo_regions()
            return a

        self._run("GrabCut con el nuevo rectángulo…", work)

    @Slot()
    def recomputeEdges(self) -> None:
        a = self._a
        if not a:
            return

        def work():
            a.seg.mask = silhouette_mask(a.image.rgb, a.image.alpha, a.seg.rect)
            a.redo_landmarks()
            return a

        self._run("Recalculando bordes…", work)

    @Slot(float, float, result=int)
    def materialAt(self, x: float, y: float) -> int:
        if not self._a:
            return -1
        h, w = self._a.seg.materials.shape
        xi, yi = int(x), int(y)
        c = int(self._a.seg.materials[yi, xi]) if 0 <= xi < w and 0 <= yi < h else -1
        self.selectMaterial(c)
        return c

    @Slot(int)
    def selectMaterial(self, c: int) -> None:
        self._selected_material = -1 if c == self._selected_material else c
        self.changed.emit()
        self._bump()

    @Slot(int, str)
    def setMaterialKind(self, cluster: int, kind: str) -> None:
        if self._a:
            self._a.set_material_kind(cluster, kind)
            self.changed.emit()
            self._bump()

    # ------------------------------------------------------------------ puntos
    @Slot(str, float, float)
    def moveLandmark(self, name: str, x: float, y: float) -> None:
        a = self._a
        if not a:
            return
        a.landmarks.move(name, x, y)
        self.changed.emit()

        def work():
            a.redo_regions()
            return a

        self._run("Actualizando partes 2D…", work)

    @Slot(str)
    def resetLandmark(self, name: str) -> None:
        a = self._a
        if not a:
            return
        a.landmarks.reset(name or None)

        def work():
            a.redo_regions()
            return a

        self._run("Actualizando partes 2D…", work)

    @Slot(QUrl)
    def saveAnalysis(self, folder: QUrl) -> None:
        if not self._a:
            return
        out = Path(folder.toLocalFile()) / "analisis"
        files = self._a.save(out)
        self.message.emit(f"Análisis guardado en {files['analysis'].parent}")

    @property
    def analysis(self) -> Analysis | None:
        return self._a
