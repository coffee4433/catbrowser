import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import QtQuick.Effects
import QtQuick.Dialogs
import QtQuick3D
import QtQuick3D.Helpers

ApplicationWindow {
    id: win
    width: 1440
    height: 900
    minimumWidth: 1100
    minimumHeight: 680
    visible: true
    title: "Imagen→3D"
    flags: Qt.Window | Qt.FramelessWindowHint
    color: theme.bg
    font.family: "Inter"
    font.pixelSize: 13

    property real explode: 0
    property bool wire: false
    property bool caps: true

    QtObject {
        id: theme
        readonly property color bg: "#121417"
        readonly property color panel: "#1B1F24"
        readonly property color raised: "#22272E"
        readonly property color border: "#2A2F36"
        readonly property color accent: "#6C8CFF"
        readonly property color text: "#E6E8EB"
        readonly property color muted: "#8A93A0"
        readonly property color good: "#4CC38A"
        readonly property color bad: "#FF6B6B"
        readonly property color viewer: "#0E1013"
        readonly property int fast: 140
    }

    component Panel: Rectangle {
        radius: 16
        color: theme.panel
        border.color: theme.border
        border.width: 1
    }

    component Label2: Text {
        color: theme.text
        font.family: win.font.family
        font.pixelSize: 13
    }

    component SectionTitle: Text {
        color: theme.muted
        font.pixelSize: 11
        font.weight: Font.DemiBold
        font.letterSpacing: 1.2
        font.capitalization: Font.AllUppercase
    }

    component Chip: Button {
        id: chip
        checkable: true
        implicitHeight: 32
        leftPadding: 14
        rightPadding: 14
        contentItem: Text {
            text: chip.text
            color: chip.checked ? "#0E1013" : theme.text
            font.pixelSize: 13
            font.weight: Font.Medium
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
        }
        background: Rectangle {
            radius: 10
            color: chip.checked ? theme.accent : (chip.hovered ? theme.raised : "transparent")
            border.color: chip.checked ? theme.accent : theme.border
            Behavior on color { ColorAnimation { duration: theme.fast } }
        }
    }

    component WinButton: Rectangle {
        id: wb
        property string glyph
        property color hoverColor: theme.raised
        signal clicked
        width: 36; height: 28; radius: 8
        color: ma.containsMouse ? hoverColor : "transparent"
        Behavior on color { ColorAnimation { duration: theme.fast } }
        Text { anchors.centerIn: parent; text: wb.glyph; color: theme.text; font.pixelSize: 14 }
        MouseArea { id: ma; anchors.fill: parent; hoverEnabled: true; onClicked: wb.clicked() }
    }

    component AccentSlider: Slider {
        id: sl
        implicitHeight: 24
        background: Rectangle {
            x: sl.leftPadding; y: sl.topPadding + sl.availableHeight / 2 - height / 2
            width: sl.availableWidth; height: 4; radius: 2; color: theme.border
            Rectangle { width: sl.visualPosition * parent.width; height: parent.height; radius: 2; color: theme.accent }
        }
        handle: Rectangle {
            x: sl.leftPadding + sl.visualPosition * (sl.availableWidth - width)
            y: sl.topPadding + sl.availableHeight / 2 - height / 2
            width: 16; height: 16; radius: 8
            color: sl.pressed ? "#FFFFFF" : theme.text
            border.color: theme.accent; border.width: 2
        }
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
                Rectangle { width: 10; height: 10; radius: 5; color: theme.accent }
                Label2 { text: "Imagen→3D"; font.pixelSize: 15; font.weight: Font.DemiBold }
                Label2 { text: "Fase 1 · plantilla + visor"; color: theme.muted }
                Item { Layout.fillWidth: true }
                Label2 { text: "Plantilla"; color: theme.muted }
                ComboBox {
                    id: templateBox
                    model: app.templates
                    currentIndex: app.templates.indexOf(app.currentTemplate)
                    onActivated: (i) => app.loadTemplate(app.templates[i])
                    implicitWidth: 150
                    implicitHeight: 32
                    contentItem: Text {
                        leftPadding: 12
                        text: templateBox.displayText; color: theme.text; font.pixelSize: 13
                        verticalAlignment: Text.AlignVCenter
                    }
                    background: Rectangle { radius: 10; color: theme.raised; border.color: theme.border }
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
                            required property var modelData
                            Layout.fillWidth: true
                            height: 40
                            radius: 10
                            color: modelData.id === "template" ? Qt.rgba(0.42, 0.55, 1.0, 0.14) : "transparent"
                            opacity: modelData.implemented ? 1.0 : 0.5
                            RowLayout {
                                anchors.fill: parent
                                anchors.leftMargin: 10
                                anchors.rightMargin: 8
                                spacing: 10
                                Rectangle {
                                    width: 10; height: 10; radius: 5
                                    color: modelData.id === "template" ? theme.accent : "transparent"
                                    border.color: modelData.implemented ? theme.accent : theme.muted
                                    border.width: 2
                                }
                                Label2 { text: modelData.title; Layout.fillWidth: true }
                                Rectangle {
                                    radius: 6; height: 18; width: phaseText.implicitWidth + 10
                                    color: theme.raised
                                    Text { id: phaseText; anchors.centerIn: parent; text: "F" + modelData.phase; color: theme.muted; font.pixelSize: 10 }
                                }
                            }
                        }
                    }
                    Item { Layout.fillHeight: true }
                    Label2 {
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                        color: theme.muted
                        font.pixelSize: 11
                        text: "Los pasos atenuados se activan en fases posteriores del plan (README §14)."
                    }
                }
            }

            // ------------------------------------------------------------ visor
            ColumnLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                spacing: 12

                Item {
                    id: viewerCard
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    layer.enabled: true
                    layer.effect: MultiEffect {
                        maskEnabled: true
                        maskSource: viewerMask
                        maskThresholdMin: 0.5
                        maskSpreadAtMin: 1.0
                    }

                    View3D {
                        id: view
                        anchors.fill: parent
                        environment: SceneEnvironment {
                            backgroundMode: SceneEnvironment.Color
                            clearColor: theme.viewer
                            antialiasingMode: SceneEnvironment.MSAA
                            antialiasingQuality: SceneEnvironment.High
                            debugSettings: DebugSettings { wireframeEnabled: win.wire }
                        }

                        Node {
                            id: orbitOrigin
                            y: 92
                            PerspectiveCamera { id: camera; z: 360; fieldOfView: 34; clipNear: 1; clipFar: 5000 }
                        }
                        DirectionalLight { eulerRotation.x: -35; eulerRotation.y: -30; brightness: 1.1 }
                        DirectionalLight { eulerRotation.x: -15; eulerRotation.y: 150; brightness: 0.5 }
                        DirectionalLight { eulerRotation.x: 70; eulerRotation.y: 0; brightness: 0.25 }

                        Model {
                            eulerRotation.x: -90
                            scale: Qt.vector3d(1, 1, 1)
                            geometry: GridGeometry { horizontalLines: 41; verticalLines: 41; horizontalStep: 10; verticalStep: 10 }
                            materials: DefaultMaterial { diffuseColor: "#262B33"; lighting: DefaultMaterial.NoLighting }
                        }

                        Repeater3D {
                            model: app.parts
                            delegate: Node {
                                visible: model.partVisible
                                position: model.offset.times(win.explode)
                                Behavior on position { Vector3dAnimation { duration: 120 } }
                                Model {
                                    objectName: model.name
                                    pickable: true
                                    geometry: model.geometry
                                    materials: PrincipledMaterial {
                                        baseColor: model.selected ? theme.accent : model.partColor
                                        roughness: 0.55
                                        metalness: 0.0
                                    }
                                }
                                Model {
                                    visible: win.caps && !!model.capGeometry
                                    geometry: model.capGeometry || null
                                    materials: PrincipledMaterial { baseColor: "#141418"; roughness: 0.9 }
                                }
                            }
                        }
                    }

                    OrbitCameraController {
                        anchors.fill: parent
                        origin: orbitOrigin
                        camera: camera
                    }

                    TapHandler {
                        acceptedButtons: Qt.LeftButton
                        onTapped: (point) => {
                            const r = view.pick(point.position.x, point.position.y)
                            app.select(r.objectHit ? r.objectHit.objectName : "")
                        }
                    }

                    Column {
                        anchors.left: parent.left
                        anchors.top: parent.top
                        anchors.margins: 18
                        spacing: 2
                        Label2 { text: app.currentTemplate; font.pixelSize: 18; font.weight: Font.DemiBold }
                        Label2 {
                            color: theme.muted
                            text: (app.qa.vertices || 0) + " vértices · " + (app.qa.faces || 0) + " quads · "
                                  + (app.qa.parts || 0) + " piezas · " + (app.qa.bones || 0) + " huesos"
                        }
                    }
                    Label2 {
                        anchors.right: parent.right
                        anchors.bottom: parent.bottom
                        anchors.margins: 16
                        color: theme.muted
                        font.pixelSize: 11
                        text: "Arrastrar: orbitar · Rueda: zoom · Clic: seleccionar pieza"
                    }
                }
                Item {
                    id: viewerMask
                    width: viewerCard.width
                    height: viewerCard.height
                    layer.enabled: true
                    visible: false
                    Rectangle { anchors.fill: parent; radius: 24; color: "black" }
                }

                Panel {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 60
                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 14
                        anchors.rightMargin: 18
                        spacing: 8
                        Chip { text: "Sólido"; checked: !win.wire; onClicked: win.wire = false }
                        Chip { text: "Alambre"; checked: win.wire; onClicked: win.wire = true }
                        Chip { text: "Tapas"; checked: win.caps; onClicked: win.caps = checked }
                        Item { width: 16 }
                        Label2 { text: "Explosión"; color: theme.muted }
                        AccentSlider {
                            id: explodeSlider
                            Layout.fillWidth: true
                            from: 0; to: 1.2; value: win.explode
                            onMoved: win.explode = value
                        }
                        Label2 { text: Math.round(win.explode * 100) + " %"; color: theme.muted; Layout.preferredWidth: 44 }
                    }
                }
            }

            // ------------------------------------------------------------ inspector
            Panel {
                Layout.preferredWidth: 340
                Layout.fillHeight: true

                ScrollView {
                    id: inspector
                    anchors.fill: parent
                    anchors.margins: 16
                    contentWidth: availableWidth
                    clip: true

                    ColumnLayout {
                        width: inspector.availableWidth
                        spacing: 10

                        RowLayout {
                            Layout.fillWidth: true
                            SectionTitle { text: "Partes"; Layout.fillWidth: true }
                            Text {
                                text: "mostrar todo"; color: theme.accent; font.pixelSize: 12
                                MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: app.setAllVisible(true) }
                            }
                        }

                        ListView {
                            id: partsList
                            Layout.fillWidth: true
                            Layout.preferredHeight: 300
                            clip: true
                            model: app.parts
                            spacing: 2
                            boundsBehavior: Flickable.StopAtBounds
                            delegate: Rectangle {
                                width: partsList.width
                                height: 30
                                radius: 8
                                color: model.selected ? Qt.rgba(0.42, 0.55, 1.0, 0.18) : (rowMa.containsMouse ? theme.raised : "transparent")
                                MouseArea {
                                    id: rowMa
                                    anchors.fill: parent
                                    hoverEnabled: true
                                    onClicked: app.select(model.name)
                                }
                                RowLayout {
                                    anchors.fill: parent
                                    anchors.leftMargin: 6
                                    anchors.rightMargin: 8
                                    spacing: 8
                                    Rectangle {
                                        width: 22; height: 22; radius: 6
                                        color: eyeMa.containsMouse ? theme.border : "transparent"
                                        Text {
                                            anchors.centerIn: parent
                                            text: model.partVisible ? "◉" : "○"
                                            color: model.partVisible ? theme.accent : theme.muted
                                            font.pixelSize: 13
                                        }
                                        MouseArea {
                                            id: eyeMa
                                            anchors.fill: parent
                                            hoverEnabled: true
                                            onClicked: app.setPartVisible(index, !model.partVisible)
                                        }
                                    }
                                    Rectangle { width: 10; height: 10; radius: 3; color: model.partColor }
                                    Label2 { text: model.name; Layout.fillWidth: true; opacity: model.partVisible ? 1 : 0.5 }
                                    Label2 { text: model.block; color: theme.muted; font.pixelSize: 11 }
                                }
                            }
                        }

                        // ---- pieza seleccionada
                        Rectangle {
                            Layout.fillWidth: true
                            visible: app.selectedPart !== ""
                            implicitHeight: selCol.implicitHeight + 20
                            radius: 10
                            color: theme.raised
                            ColumnLayout {
                                id: selCol
                                anchors.fill: parent
                                anchors.margins: 10
                                spacing: 4
                                Label2 { text: app.selectedPart; font.weight: Font.DemiBold }
                                Label2 { text: "Hueso: " + (app.selectedInfo.bone || ""); color: theme.muted; font.pixelSize: 12 }
                                Label2 { text: "Material: " + (app.selectedInfo.material || ""); color: theme.muted; font.pixelSize: 12 }
                                Label2 { text: (app.selectedInfo.vertices || 0) + " vértices · " + (app.selectedInfo.faces || 0) + " quads"; color: theme.muted; font.pixelSize: 12 }
                                Label2 { text: "Anillos: " + (app.selectedInfo.rings || ""); color: theme.muted; font.pixelSize: 12; wrapMode: Text.WordWrap; Layout.fillWidth: true }
                                Label2 { text: "Grupos: " + (app.selectedInfo.groups || ""); color: theme.muted; font.pixelSize: 12; wrapMode: Text.WordWrap; Layout.fillWidth: true }
                            }
                        }

                        // ---- control de calidad
                        SectionTitle { text: "Control de calidad"; Layout.topMargin: 6 }
                        Repeater {
                            model: [
                                { label: "Piezas", value: (app.qa.parts || 0) + " / 28", ok: app.qa.parts === 28 },
                                { label: "Quads", value: Math.round((app.qa.quad_ratio || 0) * 100) + " %", ok: app.qa.quad_ratio === 1 },
                                { label: "Anillos (error máx.)", value: (app.qa.ring_max_error_cm || 0).toFixed(3) + " cm", ok: app.qa.ring_max_error_cm === 0 },
                                { label: "Aristas no-manifold", value: "" + (app.qa.non_manifold_edges || 0), ok: app.qa.non_manifold_edges === 0 },
                                { label: "Asimetría media", value: (app.qa.asymmetry_mean_cm || 0).toFixed(3) + " cm", ok: app.qa.asymmetry_mean_cm < 0.2 },
                                { label: "Landmarks · morphs", value: (app.qa.landmarks || 0) + " · " + (app.qa.morphs || 0), ok: true }
                            ]
                            delegate: RowLayout {
                                required property var modelData
                                Layout.fillWidth: true
                                Text { text: modelData.ok ? "✓" : "✕"; color: modelData.ok ? theme.good : theme.bad; font.pixelSize: 13 }
                                Label2 { text: modelData.label; Layout.fillWidth: true; color: theme.muted }
                                Label2 { text: modelData.value }
                            }
                        }

                        // ---- morphs
                        RowLayout {
                            Layout.fillWidth: true
                            Layout.topMargin: 6
                            SectionTitle { text: "Morphs"; Layout.fillWidth: true }
                            Text {
                                text: "restablecer"; color: theme.accent; font.pixelSize: 12
                                MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: app.resetMorphs() }
                            }
                        }
                        Repeater {
                            model: app.morphs
                            delegate: ColumnLayout {
                                required property var modelData
                                Layout.fillWidth: true
                                spacing: 0
                                RowLayout {
                                    Layout.fillWidth: true
                                    Label2 { text: modelData.name; Layout.fillWidth: true; font.pixelSize: 12 }
                                    Label2 { text: ms.value.toFixed(2); color: theme.muted; font.pixelSize: 12 }
                                }
                                AccentSlider {
                                    id: ms
                                    Layout.fillWidth: true
                                    from: modelData.lo; to: modelData.hi; value: modelData.value
                                    ToolTip.visible: hovered; ToolTip.text: modelData.unit
                                    onPressedChanged: if (!pressed) app.setMorph(modelData.name, value)
                                }
                            }
                        }

                        Button {
                            id: exportBtn
                            Layout.fillWidth: true
                            Layout.topMargin: 10
                            implicitHeight: 40
                            text: "Exportar .glb + manifest"
                            onClicked: folderDialog.open()
                            contentItem: Text {
                                text: exportBtn.text; color: "#0E1013"; font.pixelSize: 13; font.weight: Font.DemiBold
                                horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter
                            }
                            background: Rectangle {
                                radius: 10
                                color: exportBtn.pressed ? Qt.darker(theme.accent, 1.2) : (exportBtn.hovered ? Qt.lighter(theme.accent, 1.1) : theme.accent)
                                Behavior on color { ColorAnimation { duration: theme.fast } }
                            }
                        }
                    }
                }
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
                Label2 { text: app.status; color: theme.muted; Layout.fillWidth: true; elide: Text.ElideMiddle }
                Label2 {
                    text: app.qa.ring_max_error_cm === 0 && app.qa.quad_ratio === 1 ? "✓ Fase 1: criterios de aceptación cumplidos" : "Revisar control de calidad"
                    color: app.qa.ring_max_error_cm === 0 && app.qa.quad_ratio === 1 ? theme.good : theme.bad
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
