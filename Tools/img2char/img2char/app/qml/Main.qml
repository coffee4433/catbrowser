import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import QtQuick.Dialogs

ApplicationWindow {
    id: win
    width: 1440
    height: 900
    minimumWidth: 1100
    minimumHeight: 680
    visible: true
    title: "Imagen→3D"
    flags: Qt.Window | Qt.FramelessWindowHint
    color: Theme.bg
    font.family: Theme.font
    font.pixelSize: 13

    property string page: "template"
    property string status: app.status

    function go(stepId) {
        if (stepId === "template" || stepId === "input" || ((stepId === "mask" || stepId === "points") && photo.hasAnalysis))
            page = stepId
    }

    Connections {
        target: photo
        function onMessage(text) { win.status = text }
        function onBusyChanged() {
            if (!photo.busy && photo.hasAnalysis && win.page === "input")
                win.page = "mask"
        }
    }
    Connections {
        target: app
        function onStatusChanged() { win.status = app.status }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // ------------------------------------------------------------ barra de título
        Item {
            Layout.fillWidth: true
            Layout.preferredHeight: 52
            DragHandler { target: null; onActiveChanged: if (active) win.startSystemMove() }
            TapHandler { onDoubleTapped: win.visibility === Window.Maximized ? win.showNormal() : win.showMaximized() }
            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 20
                anchors.rightMargin: 12
                spacing: 12
                Rectangle { width: 10; height: 10; radius: 5; color: Theme.accent }
                Label2 { text: "Imagen→3D"; font.pixelSize: 15; font.weight: Font.DemiBold }
                Label2 { text: "Fases 1–2 · plantilla, segmentación y puntos"; color: Theme.muted }
                Item { Layout.fillWidth: true }
                Label2 { text: "Plantilla"; color: Theme.muted }
                ComboBox {
                    id: templateBox
                    model: app.templates
                    currentIndex: app.templates.indexOf(app.currentTemplate)
                    onActivated: (i) => app.loadTemplate(app.templates[i])
                    implicitWidth: 150
                    implicitHeight: 32
                    contentItem: Text {
                        leftPadding: 12
                        text: templateBox.displayText; color: Theme.text; font.pixelSize: 13
                        verticalAlignment: Text.AlignVCenter
                    }
                    background: Rectangle { radius: 10; color: Theme.raised; border.color: Theme.border }
                }
                Item { width: 12 }
                WinButton { glyph: "–"; onClicked: win.showMinimized() }
                WinButton { glyph: "□"; onClicked: win.visibility === Window.Maximized ? win.showNormal() : win.showMaximized() }
                WinButton { glyph: "✕"; hoverColor: "#C4314B"; onClicked: Qt.quit() }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.leftMargin: 12
            Layout.rightMargin: 12
            spacing: 12

            // ------------------------------------------------------------ pasos
            Panel {
                Layout.preferredWidth: 200
                Layout.fillHeight: true
                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 16
                    spacing: 6
                    SectionTitle { text: "Pasos" }
                    Item { height: 4 }
                    Repeater {
                        model: app.steps
                        delegate: Rectangle {
                            id: stepRow
                            required property var modelData
                            readonly property bool current: win.page === modelData.id
                            readonly property bool reachable: modelData.implemented
                                && (modelData.id === "template" || modelData.id === "input" || photo.hasAnalysis)
                            Layout.fillWidth: true
                            height: 40
                            radius: 10
                            color: current ? Qt.rgba(0.42, 0.55, 1.0, 0.14) : (stepMa.containsMouse && reachable ? Theme.raised : "transparent")
                            opacity: modelData.implemented ? (reachable ? 1.0 : 0.7) : 0.4
                            Behavior on color { ColorAnimation { duration: Theme.fast } }
                            MouseArea {
                                id: stepMa
                                anchors.fill: parent
                                hoverEnabled: true
                                cursorShape: stepRow.reachable ? Qt.PointingHandCursor : Qt.ArrowCursor
                                onClicked: win.go(stepRow.modelData.id)
                            }
                            RowLayout {
                                anchors.fill: parent
                                anchors.leftMargin: 10
                                anchors.rightMargin: 8
                                spacing: 10
                                Rectangle {
                                    width: 10; height: 10; radius: 5
                                    color: stepRow.current ? Theme.accent : "transparent"
                                    border.color: stepRow.modelData.implemented ? Theme.accent : Theme.muted
                                    border.width: 2
                                }
                                Label2 { text: stepRow.modelData.title; Layout.fillWidth: true }
                                Rectangle {
                                    radius: 6; height: 18; width: phaseText.implicitWidth + 10
                                    color: Theme.raised
                                    Text { id: phaseText; anchors.centerIn: parent; text: "F" + stepRow.modelData.phase; color: Theme.muted; font.pixelSize: 10 }
                                }
                            }
                        }
                    }
                    Item { Layout.fillHeight: true }
                    Label2 {
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                        color: Theme.muted
                        font.pixelSize: 11
                        text: "Los pasos atenuados llegan en fases posteriores (README §14). Máscara y Puntos se activan tras analizar una foto."
                    }
                }
            }

            StackLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                currentIndex: ["template", "input", "mask", "points"].indexOf(win.page)

                TemplatePage { onExportRequested: folderDialog.open() }
                InputPage {}
                MaskPage {}
                PointsPage {}
            }
        }

        // ------------------------------------------------------------ barra de estado
        Item {
            Layout.fillWidth: true
            Layout.preferredHeight: 40
            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 20
                anchors.rightMargin: 20
                spacing: 10
                BusyIndicator {
                    visible: photo.busy
                    running: photo.busy
                    implicitWidth: 20; implicitHeight: 20
                }
                Label2 {
                    text: photo.busy ? photo.busyText : win.status
                    color: Theme.muted
                    Layout.fillWidth: true
                    elide: Text.ElideMiddle
                }
                Label2 {
                    visible: win.page === "template"
                    text: app.qa.ring_max_error_cm === 0 && app.qa.quad_ratio === 1 ? "✓ Fase 1: criterios de aceptación cumplidos" : "Revisar control de calidad"
                    color: app.qa.ring_max_error_cm === 0 && app.qa.quad_ratio === 1 ? Theme.good : Theme.bad
                }
                Label2 {
                    visible: win.page !== "template" && photo.hasAnalysis
                    text: photo.warnings.length ? "⚠ " + photo.warnings.length + " aviso(s)" : "✓ Sin avisos"
                    color: photo.warnings.length ? Theme.warn : Theme.good
                }
            }
        }
    }

    FolderDialog {
        id: folderDialog
        title: "Carpeta de exportación"
        onAccepted: app.exportTo(selectedFolder)
    }
}
