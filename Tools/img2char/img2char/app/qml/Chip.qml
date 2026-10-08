import QtQuick
import QtQuick.Controls.Basic

Button {
    id: chip
    checkable: true
    implicitHeight: 32
    leftPadding: 14
    rightPadding: 14
    contentItem: Text {
        text: chip.text
        color: chip.checked ? "#0E1013" : Theme.text
        opacity: chip.enabled ? 1 : 0.4
        font.family: Theme.font
        font.pixelSize: 13
        font.weight: Font.Medium
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
    }
    background: Rectangle {
        radius: 10
        color: chip.checked ? Theme.accent : (chip.hovered ? Theme.raised : "transparent")
        border.color: chip.checked ? Theme.accent : Theme.border
        Behavior on color { ColorAnimation { duration: Theme.fast } }
    }
}
