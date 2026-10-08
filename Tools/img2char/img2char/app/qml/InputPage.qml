import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import QtQuick.Dialogs

// Paso "Entrada": ranuras de imagen (arrastrar y soltar o elegir), altura e interruptores.
RowLayout {
    id: page
    spacing: 12

    property string pendingSlot: "front"

    FileDialog {
        id: fileDialog
        title: "Elegir imagen"
        nameFilters: ["Imágenes (*.png *.jpg *.jpeg *.webp *.bmp *.tif *.tiff)"]
        onAccepted: photo.setImage(page.pendingSlot, selectedFile)
    }

    Panel {
        Layout.fillWidth: true
        Layout.fillHeight: true

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 24
            spacing: 16

            ColumnLayout {
                spacing: 4
                Label2 { text: "Entrada"; font.pixelSize: 20; font.weight: Font.DemiBold }
                Label2 {
                    color: Theme.muted
                    text: "Foto de cuerpo entero en pose A, fondo liso y a ≥ 3 m con zoom. La frontal es obligatoria; las demás vistas se usan desde la fase 4."
                    wrapMode: Text.WordWrap
                    Layout.fillWidth: true
                }
            }

            GridLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                columns: 4
                columnSpacing: 14
                rowSpacing: 14

                Repeater {
                    model: photo.slots
                    delegate: Rectangle {
                        id: slot
                        required property var modelData
                        readonly property bool filled: modelData.url !== ""
                        readonly property bool active: modelData.phase <= 2
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        Layout.preferredWidth: 1
                        radius: 16
                        color: drop.containsDrag ? Qt.rgba(0.42, 0.55, 1.0, 0.14) : Theme.raised
                        border.color: drop.containsDrag || (modelData.required && !filled) ? Theme.accent : Theme.border
                        border.width: drop.containsDrag ? 2 : 1
                        opacity: active ? 1 : 0.6
                        Behavior on color { ColorAnimation { duration: Theme.fast } }

                        Image {
                            anchors.fill: parent
                            anchors.margins: 10
                            anchors.topMargin: 40
                            anchors.bottomMargin: 10
                            source: slot.modelData.url
                            fillMode: Image.PreserveAspectFit
                            asynchronous: true
                            visible: slot.filled
                            sourceSize.height: 600
                        }
                        RowLayout {
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.top: parent.top
                            anchors.margins: 12
                            Label2 { text: slot.modelData.title + (slot.modelData.required ? " *" : ""); font.weight: Font.DemiBold; Layout.fillWidth: true }
                            Rectangle {
                                visible: !slot.active
                                radius: 6; height: 18; width: fTxt.implicitWidth + 10; color: Theme.border
                                Text { id: fTxt; anchors.centerIn: parent; text: "F" + slot.modelData.phase; color: Theme.muted; font.pixelSize: 10 }
                            }
                            LinkText { visible: slot.filled; text: "quitar"; onClicked: photo.clearImage(slot.modelData.id) }
                        }
                        Column {
                            anchors.centerIn: parent
                            visible: !slot.filled
                            spacing: 6
                            Text { anchors.horizontalCenter: parent.horizontalCenter; text: "＋"; color: Theme.muted; font.pixelSize: 30 }
                            Label2 { anchors.horizontalCenter: parent.horizontalCenter; text: "Arrastra una imagen"; color: Theme.muted }
                            Label2 { anchors.horizontalCenter: parent.horizontalCenter; text: "o haz clic para elegir"; color: Theme.muted; font.pixelSize: 11 }
                        }
                        MouseArea {
                            anchors.fill: parent
                            anchors.topMargin: 40
                            cursorShape: Qt.PointingHandCursor
                            onClicked: { page.pendingSlot = slot.modelData.id; fileDialog.open() }
                        }
                        DropArea {
                            id: drop
                            anchors.fill: parent
                            onDropped: (d) => { if (d.hasUrls) photo.setImage(slot.modelData.id, d.urls[0]) }
                        }
                    }
                }
            }
        }
    }

    // ------------------------------------------------------------ ajustes
    Panel {
        Layout.preferredWidth: 340
        Layout.fillHeight: true

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 16
            spacing: 12

            SectionTitle { text: "Personaje" }
            RowLayout {
                Layout.fillWidth: true
                Label2 { text: "Altura"; Layout.fillWidth: true }
                SpinBox {
                    id: heightBox
                    from: 40; to: 300; editable: true
                    value: Math.round(photo.heightCm)
                    onValueModified: photo.setHeightCm(value)
                    implicitWidth: 130
                    implicitHeight: 34
                    contentItem: TextInput {
                        text: heightBox.textFromValue(heightBox.value, heightBox.locale) + " cm"
                        color: Theme.text; font.pixelSize: 13
                        horizontalAlignment: Qt.AlignHCenter; verticalAlignment: Qt.AlignVCenter
                        readOnly: !heightBox.editable
                        validator: heightBox.validator
                        inputMethodHints: Qt.ImhFormattedNumbersOnly
                    }
                    background: Rectangle { radius: 10; color: Theme.raised; border.color: Theme.border }
                }
            }
            Label2 {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                color: Theme.muted
                font.pixelSize: 11
                text: "Da la escala: px/cm = altura en píxeles / altura en cm (cámara ortográfica, §5)."
            }

            SectionTitle { text: "Opciones"; Layout.topMargin: 8 }
            RowLayout {
                Layout.fillWidth: true
                Label2 { text: "Balance de blancos (gray-world)"; Layout.fillWidth: true }
                Switch {
                    checked: photo.whiteBalance
                    onToggled: photo.setWhiteBalance(checked)
                }
            }

            Item { Layout.fillHeight: true }

            Rectangle {
                Layout.fillWidth: true
                implicitHeight: sampleCol.implicitHeight + 24
                radius: 12
                color: Theme.raised
                ColumnLayout {
                    id: sampleCol
                    anchors.fill: parent
                    anchors.margins: 12
                    spacing: 6
                    Label2 { text: "¿Sin foto a mano?"; font.weight: Font.DemiBold }
                    Label2 {
                        Layout.fillWidth: true; wrapMode: Text.WordWrap; color: Theme.muted; font.pixelSize: 12
                        text: "Genera una foto de prueba renderizada desde la plantilla con morphs aleatorios."
                    }
                    LinkText { text: "Usar foto de prueba (" + app.currentTemplate + ")"; onClicked: photo.useSample(app.currentTemplate) }
                }
            }

            PrimaryButton {
                Layout.fillWidth: true
                text: photo.busy ? "Analizando…" : "Analizar foto frontal"
                enabled: photo.canAnalyze
                onClicked: photo.analyze()
            }
        }
    }
}
