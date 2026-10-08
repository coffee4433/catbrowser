import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

// Paso "Máscara": silueta, pincel añadir/quitar, rectángulo de GrabCut, partes 2D y materiales.
RowLayout {
    id: page
    spacing: 12

    property string mode: "mask"          // mask | materials | parts
    property string tool: "add"           // add | remove | rect | pick
    property real brush: 12

    ColumnLayout {
        Layout.fillWidth: true
        Layout.fillHeight: true
        spacing: 12

        ImageView {
            id: view
            Layout.fillWidth: true
            Layout.fillHeight: true
            sourceW: photo.imageWidth
            sourceH: photo.imageHeight
            source: photo.hasAnalysis ? "image://overlay/" + page.mode + "/" + photo.revision : ""

            // Cursor del pincel
            Rectangle {
                visible: (page.tool === "add" || page.tool === "remove") && hover.hovered && page.mode === "mask"
                x: hover.point.position.x - width / 2
                y: hover.point.position.y - height / 2
                width: 2 * page.brush * view.s; height: width; radius: width / 2
                color: "transparent"
                border.color: page.tool === "add" ? Theme.good : Theme.bad
                border.width: 2
            }
            // Rectángulo de GrabCut en curso
            Rectangle {
                id: rectShape
                visible: false
                color: Qt.rgba(0.42, 0.55, 1.0, 0.12)
                border.color: Theme.accent
                border.width: 2
            }
            HoverHandler { id: hover }

            MouseArea {
                anchors.fill: parent
                enabled: photo.hasAnalysis && !photo.busy
                property point last
                property point start
                cursorShape: page.tool === "rect" ? Qt.CrossCursor : (page.tool === "pick" ? Qt.PointingHandCursor : Qt.BlankCursor)
                onPressed: (m) => {
                    const p = view.toImage(m.x, m.y)
                    if (page.tool === "pick" || page.mode === "materials") {
                        photo.materialAt(p.x, p.y)
                    } else if (page.tool === "rect") {
                        start = Qt.point(m.x, m.y)
                        rectShape.x = m.x; rectShape.y = m.y; rectShape.width = 0; rectShape.height = 0
                        rectShape.visible = true
                    } else {
                        last = p
                        photo.paintStroke(p.x, p.y, p.x, p.y, page.brush, page.tool === "add")
                    }
                }
                onPositionChanged: (m) => {
                    if (!pressed) return
                    if (page.tool === "rect") {
                        rectShape.x = Math.min(start.x, m.x); rectShape.y = Math.min(start.y, m.y)
                        rectShape.width = Math.abs(m.x - start.x); rectShape.height = Math.abs(m.y - start.y)
                    } else if (page.tool === "add" || page.tool === "remove") {
                        const p = view.toImage(m.x, m.y)
                        photo.paintStroke(last.x, last.y, p.x, p.y, page.brush, page.tool === "add")
                        last = p
                    }
                }
                onReleased: (m) => {
                    if (page.tool === "rect") {
                        rectShape.visible = false
                        const a = view.toImage(rectShape.x, rectShape.y)
                        const b = view.toImage(rectShape.x + rectShape.width, rectShape.y + rectShape.height)
                        photo.setRect(a.x, a.y, b.x - a.x, b.y - a.y)
                    } else if (page.tool === "add" || page.tool === "remove") {
                        photo.commitMask()
                    }
                }
            }
        }

        Panel {
            Layout.fillWidth: true
            Layout.preferredHeight: 60
            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 14
                anchors.rightMargin: 18
                spacing: 8
                Chip { text: "Silueta"; checked: page.mode === "mask"; onClicked: page.mode = "mask" }
                Chip { text: "Partes 2D"; checked: page.mode === "parts"; onClicked: page.mode = "parts" }
                Chip { text: "Materiales"; checked: page.mode === "materials"; onClicked: { page.mode = "materials"; page.tool = "pick" } }
                Rectangle { width: 1; height: 28; color: Theme.border }
                Chip { text: "＋ Añadir"; checked: page.tool === "add"; enabled: page.mode === "mask"; onClicked: page.tool = "add" }
                Chip { text: "－ Quitar"; checked: page.tool === "remove"; enabled: page.mode === "mask"; onClicked: page.tool = "remove" }
                Chip { text: "▭ Rectángulo"; checked: page.tool === "rect"; enabled: page.mode === "mask"; onClicked: page.tool = "rect" }
                Item { width: 8 }
                Label2 { text: "Pincel"; color: Theme.muted }
                AccentSlider {
                    Layout.fillWidth: true
                    from: 2; to: 60; value: page.brush
                    onMoved: page.brush = value
                }
                Label2 { text: Math.round(page.brush) + " px"; color: Theme.muted; Layout.preferredWidth: 44 }
            }
        }
    }

    // ------------------------------------------------------------ inspector
    Panel {
        Layout.preferredWidth: 340
        Layout.fillHeight: true

        ScrollView {
            id: insp
            anchors.fill: parent
            anchors.margins: 16
            contentWidth: availableWidth
            clip: true

            ColumnLayout {
                width: insp.availableWidth
                spacing: 10

                SectionTitle { text: "Silueta" }
                Repeater {
                    model: [
                        { label: "Píxeles de silueta", value: (photo.stats.maskPx || 0).toLocaleString(Qt.locale(), "f", 0) },
                        { label: "Altura en la imagen", value: Math.round(photo.stats.heightPx || 0) + " px" },
                        { label: "Escala", value: (photo.stats.pxPerCm || 0).toFixed(2) + " px/cm" },
                        { label: "Proporción", value: (photo.stats.heights || 0).toFixed(2) + " cabezas" }
                    ]
                    delegate: RowLayout {
                        required property var modelData
                        Layout.fillWidth: true
                        Label2 { text: modelData.label; color: Theme.muted; Layout.fillWidth: true }
                        Label2 { text: modelData.value }
                    }
                }
                LinkText { text: "Recalcular bordes (GrabCut + Sobel)"; onClicked: photo.recomputeEdges() }
                Label2 {
                    Layout.fillWidth: true; wrapMode: Text.WordWrap; color: Theme.muted; font.pixelSize: 11
                    text: "Pincel: arrastra para añadir o quitar; al soltar se recalculan puntos, partes y materiales. Rectángulo: dibuja una caja alrededor del personaje para repetir GrabCut."
                }

                SectionTitle { text: "Materiales"; Layout.topMargin: 8 }
                Label2 {
                    Layout.fillWidth: true; wrapMode: Text.WordWrap; color: Theme.muted; font.pixelSize: 11
                    text: "Haz clic en una zona de la imagen (modo Materiales) para resaltarla y cambia su etiqueta aquí."
                }
                Repeater {
                    model: photo.materials
                    delegate: Rectangle {
                        id: matRow
                        required property var modelData
                        Layout.fillWidth: true
                        implicitHeight: 40
                        radius: 10
                        color: photo.selectedMaterial === modelData.id ? Qt.rgba(0.42, 0.55, 1.0, 0.18) : "transparent"
                        MouseArea { anchors.fill: parent; onClicked: { page.mode = "materials"; photo.selectMaterial(matRow.modelData.id) } }
                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: 8
                            anchors.rightMargin: 4
                            spacing: 8
                            Rectangle { width: 22; height: 22; radius: 6; color: matRow.modelData.swatch; border.color: Theme.border }
                            Rectangle { width: 6; height: 22; radius: 3; color: matRow.modelData.kindColor }
                            Label2 { text: (matRow.modelData.share * 100).toFixed(1) + " %"; color: Theme.muted; Layout.preferredWidth: 52 }
                            ComboBox {
                                id: kindBox
                                Layout.fillWidth: true
                                implicitHeight: 30
                                model: photo.materialKinds
                                textRole: "label"
                                valueRole: "id"
                                Component.onCompleted: currentIndex = indexOfValue(matRow.modelData.kind)
                                onActivated: photo.setMaterialKind(matRow.modelData.id, currentValue)
                                contentItem: Text {
                                    leftPadding: 10
                                    text: kindBox.displayText; color: Theme.text; font.pixelSize: 12
                                    verticalAlignment: Text.AlignVCenter
                                }
                                background: Rectangle { radius: 8; color: Theme.raised; border.color: Theme.border }
                            }
                        }
                    }
                }

                SectionTitle { text: "Partes 2D"; Layout.topMargin: 8 }
                Flow {
                    Layout.fillWidth: true
                    spacing: 8
                    Repeater {
                        model: photo.partsLegend
                        delegate: Row {
                            required property var modelData
                            spacing: 5
                            Rectangle { width: 10; height: 10; radius: 3; color: modelData.color; anchors.verticalCenter: parent.verticalCenter }
                            Label2 { text: modelData.label; font.pixelSize: 12; color: Theme.muted }
                        }
                    }
                }

                SectionTitle { text: "Avisos"; Layout.topMargin: 8; visible: photo.warnings.length > 0 }
                Repeater {
                    model: photo.warnings
                    delegate: Label2 {
                        required property var modelData
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                        color: Theme.warn
                        font.pixelSize: 12
                        text: "⚠ " + modelData
                    }
                }
            }
        }
    }
}
