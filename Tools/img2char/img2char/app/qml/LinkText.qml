import QtQuick

Text {
    id: link
    signal clicked
    color: ma.containsMouse ? Qt.lighter(Theme.accent, 1.2) : Theme.accent
    font.family: Theme.font
    font.pixelSize: 12
    MouseArea { id: ma; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: link.clicked() }
}
