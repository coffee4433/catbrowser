"""Aplicación de escritorio (README §12): PySide6 + QML + Qt Quick 3D.

Fase 1: carga la plantilla, lista las 28 piezas, vista explosionada, tapas, alambre, control de
calidad, sliders de morphs y exportación .glb.
"""
from __future__ import annotations

import sys
from pathlib import Path

import numpy as np
from PySide6.QtCore import (Property, QAbstractListModel, QByteArray, QModelIndex, QObject, Qt, QUrl, Signal,
                            Slot)
from PySide6.QtGui import QColor, QGuiApplication, QVector3D
from PySide6.QtQml import QQmlApplicationEngine
from PySide6.QtQuick3D import QQuick3DGeometry

from ..core.export import export_character
from ..core.export.gltf import cap_render_arrays, part_render_arrays
from ..core.parts import explode_offsets, quality_report, split_parts
from ..core.template.model import Template, ensure_templates
from ..core.template.parts_def import MATERIAL_COLORS
from ..pipeline import STEPS

QML_DIR = Path(__file__).with_name("qml")


class PartGeometry(QQuick3DGeometry):
    """Geometría de una pieza (posición + normal, índices u32) para un Model de Qt Quick 3D.

    Sin padre QObject: el Controller guarda la referencia para que Python no la libere.
    """

    def __init__(self, parent=None):
        super().__init__(parent)

    def set_arrays(self, pos: np.ndarray, nrm: np.ndarray, tris: np.ndarray) -> None:
        self.clear()
        data = np.concatenate([pos, nrm], 1).astype(np.float32)
        self.setVertexData(QByteArray(data.tobytes()))
        self.setIndexData(QByteArray(tris.astype(np.uint32).tobytes()))
        self.setStride(24)
        self.setPrimitiveType(QQuick3DGeometry.PrimitiveType.Triangles)
        A = QQuick3DGeometry.Attribute
        self.addAttribute(A.PositionSemantic, 0, A.ComponentType.F32Type)
        self.addAttribute(A.NormalSemantic, 12, A.ComponentType.F32Type)
        self.addAttribute(A.IndexSemantic, 0, A.ComponentType.U32Type)
        lo, hi = pos.min(0), pos.max(0)
        self.setBounds(QVector3D(*lo), QVector3D(*hi))
        self.update()


class PartsModel(QAbstractListModel):
    NameRole, BlockRole, ColorRole, VisibleRole, GeometryRole, CapRole, OffsetRole, InfoRole, SelectedRole = \
        range(Qt.UserRole + 1, Qt.UserRole + 10)

    def __init__(self, parent=None):
        super().__init__(parent)
        self.rows: list[dict] = []

    def roleNames(self):
        return {self.NameRole: b"name", self.BlockRole: b"block", self.ColorRole: b"partColor",
                self.VisibleRole: b"partVisible", self.GeometryRole: b"geometry", self.CapRole: b"capGeometry",
                self.OffsetRole: b"offset", self.InfoRole: b"info", self.SelectedRole: b"selected"}

    def rowCount(self, parent=QModelIndex()):
        return 0 if parent.isValid() else len(self.rows)

    def data(self, index, role):
        if not index.isValid():
            return None
        r = self.rows[index.row()]
        return {self.NameRole: r["name"], self.BlockRole: r["block"], self.ColorRole: r["color"],
                self.VisibleRole: r["visible"], self.GeometryRole: r["geometry"], self.CapRole: r["cap"],
                self.OffsetRole: r["offset"], self.InfoRole: r["info"], self.SelectedRole: r["selected"]}.get(role)

    def reset(self, rows: list[dict]) -> None:
        self.beginResetModel()
        self.rows = rows
        self.endResetModel()

    def set_field(self, row: int, key: str, value, role: int) -> None:
        self.rows[row][key] = value
        i = self.index(row)
        self.dataChanged.emit(i, i, [role])


class Controller(QObject):
    changed = Signal()
    statusChanged = Signal()
    morphsChanged = Signal()

    def __init__(self, template_dir: Path):
        super().__init__()
        self._dir = template_dir
        self._templates = ensure_templates(template_dir)
        self._model = PartsModel(self)
        self._tpl: Template | None = None
        self._alpha = np.zeros(0)
        self._qa: dict = {}
        self._status = ""
        self._selected = ""
        self._geoms: dict[str, tuple[PartGeometry, PartGeometry | None]] = {}
        self.loadTemplate("female_v1" if "female_v1" in self._templates else self._templates[0])

    # ------------------------------------------------------------- propiedades
    @Property(list, constant=True)
    def templates(self):
        return self._templates

    @Property(str, notify=changed)
    def currentTemplate(self):
        return self._tpl.name if self._tpl else ""

    @Property(QObject, constant=True)
    def parts(self):
        return self._model

    @Property("QVariantMap", notify=changed)
    def qa(self):
        return self._qa

    @Property(str, notify=statusChanged)
    def status(self):
        return self._status

    @Property(str, notify=changed)
    def selectedPart(self):
        return self._selected

    @Property("QVariantMap", notify=changed)
    def selectedInfo(self):
        for r in self._model.rows:
            if r["name"] == self._selected:
                return r["info"]
        return {}

    @Property("QVariantList", notify=morphsChanged)
    def morphs(self):
        if not self._tpl:
            return []
        return [{"name": m["name"], "lo": m["lo"], "hi": m["hi"], "unit": m["unit"], "value": float(a)}
                for m, a in zip(self._tpl.morph_info, self._alpha)]

    @Property("QVariantList", constant=True)
    def steps(self):
        return [{"id": s.id, "title": s.title, "phase": s.phase, "implemented": s.implemented} for s in STEPS]

    def _set_status(self, text: str) -> None:
        self._status = text
        self.statusChanged.emit()

    # ----------------------------------------------------------------- acciones
    @Slot(str)
    def loadTemplate(self, name: str) -> None:
        self._tpl = Template.load(self._dir / name)
        self._alpha = np.zeros(len(self._tpl.morph_names))
        self._selected = ""
        self._geoms.clear()
        self._rebuild(reset=True)
        self.morphsChanged.emit()
        self._set_status(f"Plantilla {name} cargada · {len(self._tpl.V0)} vértices · "
                         f"{self._qa['parts']} piezas")

    def _rebuild(self, reset: bool) -> None:
        V = self._tpl.deformed(self._alpha if self._alpha.any() else None)
        parts = split_parts(self._tpl, V)
        offsets = explode_offsets(parts)
        rows = []
        for pid, p in parts.items():
            pos, nrm, _, tris = part_render_arrays(p)
            geom, capg = self._geoms.get(pid, (None, None))
            if geom is None:
                geom = PartGeometry()
            geom.set_arrays(pos, nrm, tris)
            cap = cap_render_arrays(p)
            if cap is not None:
                capg = capg or PartGeometry()
                capg.set_arrays(*cap)
            self._geoms[pid] = (geom, capg)
            d = p.definition
            rows.append({"name": pid, "block": d.block, "color": QColor.fromRgbF(*MATERIAL_COLORS[d.material]),
                         "visible": True, "geometry": geom, "cap": capg, "offset": QVector3D(*offsets[pid]),
                         "selected": pid == self._selected,
                         "info": {"vertices": len(p.V), "faces": len(p.F), "material": d.material, "bone": d.bone,
                                  "rings": ", ".join(sorted(p.rings)) or "—",
                                  "groups": ", ".join(d.groups) or "—"}})
        report = quality_report(self._tpl, parts, V)
        report.pop("per_part")
        report.pop("rings")
        self._qa = report
        if reset:
            self._model.reset(rows)
        else:
            for i, r in enumerate(rows):
                r["visible"] = self._model.rows[i]["visible"]
                self._model.set_field(i, "offset", r["offset"], PartsModel.OffsetRole)
        self.changed.emit()

    @Slot(int, bool)
    def setPartVisible(self, row: int, visible: bool) -> None:
        self._model.set_field(row, "visible", visible, PartsModel.VisibleRole)

    @Slot(bool)
    def setAllVisible(self, visible: bool) -> None:
        for i in range(len(self._model.rows)):
            self.setPartVisible(i, visible)

    @Slot(str)
    def select(self, name: str) -> None:
        self._selected = "" if name == self._selected else name
        for i, r in enumerate(self._model.rows):
            self._model.set_field(i, "selected", r["name"] == self._selected, PartsModel.SelectedRole)
        self.changed.emit()

    @Slot(str, float)
    def setMorph(self, name: str, value: float) -> None:
        i = self._tpl.morph_names.index(name)
        if abs(self._alpha[i] - value) < 1e-6:
            return
        self._alpha[i] = value
        self._rebuild(reset=False)
        self.morphsChanged.emit()

    @Slot()
    def resetMorphs(self) -> None:
        self._alpha[:] = 0
        self._rebuild(reset=False)
        self.morphsChanged.emit()

    @Slot(QUrl)
    def exportTo(self, folder: QUrl) -> None:
        out = Path(folder.toLocalFile()) / f"{self._tpl.name}_export"
        try:
            paths = export_character(self._tpl, out, self._alpha if self._alpha.any() else None)
            self._set_status(f"Exportado: {paths['glb']}")
        except Exception as e:  # la UI debe mostrar el error en vez de cerrarse
            self._set_status(f"Error al exportar: {e}")


def run(template_dir: Path, argv=None) -> int:
    app = QGuiApplication(argv or sys.argv)
    app.setApplicationName("Imagen→3D")
    ctrl = Controller(template_dir)
    engine = QQmlApplicationEngine()
    engine.rootContext().setContextProperty("app", ctrl)
    engine.load(QUrl.fromLocalFile(str(QML_DIR / "Main.qml")))
    if not engine.rootObjects():
        return 1
    code = app.exec()
    del engine  # antes que el Controller: evita bindings QML evaluados contra un contexto ya destruido
    return code
