import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import QtQuick.Effects
import QtQuick3D
import QtQuick3D.Helpers

// Paso "Plantilla" (fase 1): visor 3D de la plantilla, piezas, control de calidad y morphs.
RowLayout {
    id: page
    property real explode: 0
    property bool wire: false
    property bool caps: true
    signal exportRequested
    spacing: 12

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
                    clearColor: Theme.viewer
                    antialiasingMode: SceneEnvironment.MSAA
                    antialiasingQuality: SceneEnvironment.High
                    debugSettings: DebugSettings { wireframeEnabled: page.wire }
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
                        position: model.offset.times(page.explode)
                        Behavior on position { Vector3dAnimation { duration: 120 } }
                        Model {
                            objectName: model.name
                            pickable: true
                            geometry: model.geometry
                            materials: PrincipledMaterial {
                                baseColor: model.selected ? Theme.accent : model.partColor
                                roughness: 0.55
                                metalness: 0.0
                            }
                        }
                        Model {
                            visible: page.caps && !!model.capGeometry
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
                    color: Theme.muted
                    text: (app.qa.vertices || 0) + " vértices · " + (app.qa.faces || 0) + " quads · "
                          + (app.qa.parts || 0) + " piezas · " + (app.qa.bones || 0) + " huesos"
                }
            }
            Label2 {
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                anchors.margins: 16
                color: Theme.muted
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
                Chip { text: "Sólido"; checked: !page.wire; onClicked: page.wire = false }
                Chip { text: "Alambre"; checked: page.wire; onClicked: page.wire = true }
                Chip { text: "Tapas"; checked: page.caps; onClicked: page.caps = checked }
                Item { width: 16 }
                Label2 { text: "Explosión"; color: Theme.muted }
                AccentSlider {
                    id: explodeSlider
                    Layout.fillWidth: true
                    from: 0; to: 1.2; value: page.explode
                    onMoved: page.explode = value
                }
                Label2 { text: Math.round(page.explode * 100) + " %"; color: Theme.muted; Layout.preferredWidth: 44 }
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
                        text: "mostrar todo"; color: Theme.accent; font.pixelSize: 12
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
                        color: model.selected ? Qt.rgba(0.42, 0.55, 1.0, 0.18) : (rowMa.containsMouse ? Theme.raised : "transparent")
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
                                color: eyeMa.containsMouse ? Theme.border : "transparent"
                                Text {
                                    anchors.centerIn: parent
                                    text: model.partVisible ? "◉" : "○"
                                    color: model.partVisible ? Theme.accent : Theme.muted
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
                            Label2 { text: model.block; color: Theme.muted; font.pixelSize: 11 }
                        }
                    }
                }

                // ---- pieza seleccionada
                Rectangle {
                    Layout.fillWidth: true
                    visible: app.selectedPart !== ""
                    implicitHeight: selCol.implicitHeight + 20
                    radius: 10
                    color: Theme.raised
                    ColumnLayout {
                        id: selCol
                        anchors.fill: parent
                        anchors.margins: 10
                        spacing: 4
                        Label2 { text: app.selectedPart; font.weight: Font.DemiBold }
                        Label2 { text: "Hueso: " + (app.selectedInfo.bone || ""); color: Theme.muted; font.pixelSize: 12 }
                        Label2 { text: "Material: " + (app.selectedInfo.material || ""); color: Theme.muted; font.pixelSize: 12 }
                        Label2 { text: (app.selectedInfo.vertices || 0) + " vértices · " + (app.selectedInfo.faces || 0) + " quads"; color: Theme.muted; font.pixelSize: 12 }
                        Label2 { text: "Anillos: " + (app.selectedInfo.rings || ""); color: Theme.muted; font.pixelSize: 12; wrapMode: Text.WordWrap; Layout.fillWidth: true }
                        Label2 { text: "Grupos: " + (app.selectedInfo.groups || ""); color: Theme.muted; font.pixelSize: 12; wrapMode: Text.WordWrap; Layout.fillWidth: true }
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
                        Text { text: modelData.ok ? "✓" : "✕"; color: modelData.ok ? Theme.good : Theme.bad; font.pixelSize: 13 }
                        Label2 { text: modelData.label; Layout.fillWidth: true; color: Theme.muted }
                        Label2 { text: modelData.value }
                    }
                }

                // ---- morphs
                RowLayout {
                    Layout.fillWidth: true
                    Layout.topMargin: 6
                    SectionTitle { text: "Morphs"; Layout.fillWidth: true }
                    Text {
                        text: "restablecer"; color: Theme.accent; font.pixelSize: 12
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
                            Label2 { text: ms.value.toFixed(2); color: Theme.muted; font.pixelSize: 12 }
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
                    onClicked: page.exportRequested()
                    contentItem: Text {
                        text: exportBtn.text; color: "#0E1013"; font.pixelSize: 13; font.weight: Font.DemiBold
                        horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter
                    }
                    background: Rectangle {
                        radius: 10
                        color: exportBtn.pressed ? Qt.darker(Theme.accent, 1.2) : (exportBtn.hovered ? Qt.lighter(Theme.accent, 1.1) : Theme.accent)
                        Behavior on color { ColorAnimation { duration: Theme.fast } }
                    }
                }
            }
        }
    }
}
