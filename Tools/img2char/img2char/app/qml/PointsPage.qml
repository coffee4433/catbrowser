import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

// Paso "Puntos": landmarks y esqueleto 2D arrastrables con guías de proporción en cabezas.
RowLayout {
    id: page
    spacing: 12

    property bool guides: true
    property string hovered: ""

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
            source: photo.hasAnalysis ? "image://overlay/points/" + photo.revision : ""

            // Guías: una línea por cabeza desde la coronilla.
            Repeater {
                model: page.guides && photo.hasAnalysis ? Math.ceil((photo.stats.heights || 0)) + 1 : 0
                delegate: Item {
                    required property int index
                    readonly property real py: (photo.stats.top || 0) + index * (photo.stats.headPx || 1)
                    x: 0
                    y: view.toItem(0, py).y
                    width: view.width
                    height: 1
                    Rectangle { width: parent.width; height: 1; color: Qt.rgba(1, 1, 1, 0.12) }
                    Text { x: 10; y: -14; text: index; color: Qt.rgba(1, 1, 1, 0.35); font.pixelSize: 11 }
                }
            }

            Canvas {
                id: bonesCanvas
                anchors.fill: parent
                property var bones: photo.bones
                onBonesChanged: requestPaint()
                onWidthChanged: requestPaint()
                onHeightChanged: requestPaint()
                onPaint: {
                    const ctx = getContext("2d")
                    ctx.reset()
                    ctx.lineWidth = 2
                    ctx.strokeStyle = "rgba(255,255,255,0.75)"
                    ctx.lineCap = "round"
                    for (const b of bones) {
                        const a = view.toItem(b[0], b[1]), c = view.toItem(b[2], b[3])
                        ctx.beginPath(); ctx.moveTo(a.x, a.y); ctx.lineTo(c.x, c.y); ctx.stroke()
                    }
                }
            }

            Repeater {
                model: photo.landmarks
                delegate: Rectangle {
                    id: handle
                    required property var modelData
                    readonly property point pos: view.toItem(modelData.x, modelData.y)
                    readonly property color tone: modelData.manual ? Theme.accent : (modelData.confidence >= 0.5 ? Theme.good : Theme.bad)
                    width: dragArea.drag.active || dragArea.containsMouse ? 18 : 13
                    height: width
                    radius: width / 2
                    x: dragArea.drag.active ? x : pos.x - width / 2
                    y: dragArea.drag.active ? y : pos.y - height / 2
                    color: tone
                    border.color: "#0E1013"
                    border.width: 2
                    Behavior on width { NumberAnimation { duration: 100 } }

                    MouseArea {
                        id: dragArea
                        anchors.fill: parent
                        anchors.margins: -6
                        hoverEnabled: true
                        cursorShape: Qt.SizeAllCursor
                        enabled: !photo.busy
                        drag.target: handle
                        drag.threshold: 0
                        onContainsMouseChanged: page.hovered = containsMouse ? handle.modelData.label : ""
                        onReleased: {
                            const p = view.toImage(handle.x + handle.width / 2, handle.y + handle.height / 2)
                            photo.moveLandmark(handle.modelData.name, p.x, p.y)
                        }
                    }
                    Rectangle {
                        visible: dragArea.containsMouse || dragArea.drag.active
                        x: parent.width + 6; y: -4
                        radius: 6
                        width: tip.implicitWidth + 12; height: 22
                        color: Theme.panel
                        border.color: Theme.border
                        Text { id: tip; anchors.centerIn: parent; text: handle.modelData.label; color: Theme.text; font.pixelSize: 12 }
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
                spacing: 12
                Chip { text: "Guías de cabezas"; checked: page.guides; onClicked: page.guides = checked }
                Row {
                    spacing: 14
                    Repeater {
                        model: [{ c: Theme.good, t: "detectado" }, { c: Theme.bad, t: "por proporción" }, { c: Theme.accent, t: "movido a mano" }]
                        delegate: Row {
                            required property var modelData
                            spacing: 6
                            Rectangle { width: 10; height: 10; radius: 5; color: modelData.c; anchors.verticalCenter: parent.verticalCenter }
                            Label2 { text: modelData.t; color: Theme.muted; font.pixelSize: 12 }
                        }
                    }
                }
                Item { Layout.fillWidth: true }
                LinkText { text: "Restablecer todos"; onClicked: photo.resetLandmark("") }
            }
        }
    }

    Panel {
        Layout.preferredWidth: 340
        Layout.fillHeight: true

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 16
            spacing: 10

            SectionTitle { text: "Proporciones" }
            RowLayout {
                Layout.fillWidth: true
                Label2 { text: "Altura en cabezas"; color: Theme.muted; Layout.fillWidth: true }
                Label2 { text: (photo.stats.heights || 0).toFixed(2); font.weight: Font.DemiBold }
            }
            Label2 {
                Layout.fillWidth: true; wrapMode: Text.WordWrap; color: Theme.muted; font.pixelSize: 11
                text: "7,5 ≈ realista · 5–6 ≈ estilizado. Arrastra los puntos: al soltar se recalculan las partes 2D."
            }

            SectionTitle { text: "Puntos"; Layout.topMargin: 6 }
            ListView {
                id: list
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                model: photo.landmarks
                spacing: 2
                boundsBehavior: Flickable.StopAtBounds
                delegate: Rectangle {
                    id: row
                    required property var modelData
                    width: list.width
                    height: 28
                    radius: 8
                    color: page.hovered === modelData.label ? Theme.raised : "transparent"
                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 8
                        anchors.rightMargin: 8
                        spacing: 8
                        Rectangle {
                            width: 8; height: 8; radius: 4
                            color: row.modelData.manual ? Theme.accent : (row.modelData.confidence >= 0.5 ? Theme.good : Theme.bad)
                        }
                        Label2 { text: row.modelData.label; Layout.fillWidth: true; font.pixelSize: 12 }
                        Label2 { text: Math.round(row.modelData.x) + ", " + Math.round(row.modelData.y); color: Theme.muted; font.pixelSize: 11 }
                        LinkText { visible: row.modelData.manual; text: "↺"; onClicked: photo.resetLandmark(row.modelData.name) }
                    }
                }
            }

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
