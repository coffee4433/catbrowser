import QtQuick

Rectangle {
    id: wb
    property string glyph
    property color hoverColor: Theme.raised
    signal clicked
    width: 36; height: 28; radius: 8
    color: ma.containsMouse ? hoverColor : "transparent"
    Behavior on color { ColorAnimation { duration: Theme.fast } }
    Text { anchors.centerIn: parent; text: wb.glyph; color: Theme.text; font.pixelSize: 14 }
    MouseArea { id: ma; anchors.fill: parent; hoverEnabled: true; onClicked: wb.clicked() }
}
