import QtQuick
import QtQuick.Controls.Basic

Button {
    id: btn
    implicitHeight: 40
    contentItem: Text {
        text: btn.text
        color: "#0E1013"
        opacity: btn.enabled ? 1 : 0.5
        font.family: Theme.font
        font.pixelSize: 13
        font.weight: Font.DemiBold
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
    }
    background: Rectangle {
        radius: 10
        color: !btn.enabled ? Theme.border
             : btn.pressed ? Qt.darker(Theme.accent, 1.2)
             : (btn.hovered ? Qt.lighter(Theme.accent, 1.1) : Theme.accent)
        Behavior on color { ColorAnimation { duration: Theme.fast } }
    }
}
