import QtQuick
import QtQuick.Controls.Basic

Slider {
    id: sl
    implicitHeight: 24
    background: Rectangle {
        x: sl.leftPadding; y: sl.topPadding + sl.availableHeight / 2 - height / 2
        width: sl.availableWidth; height: 4; radius: 2; color: Theme.border
        Rectangle { width: sl.visualPosition * parent.width; height: parent.height; radius: 2; color: Theme.accent }
    }
    handle: Rectangle {
        x: sl.leftPadding + sl.visualPosition * (sl.availableWidth - width)
        y: sl.topPadding + sl.availableHeight / 2 - height / 2
        width: 16; height: 16; radius: 8
        color: sl.pressed ? "#FFFFFF" : Theme.text
        border.color: Theme.accent; border.width: 2
    }
}
