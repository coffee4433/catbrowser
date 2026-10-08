import QtQuick

// Imagen ajustada a la tarjeta con conversión entre coordenadas de ítem y píxeles de la imagen.
Rectangle {
    id: view
    property alias source: img.source
    property int sourceW: 1
    property int sourceH: 1
    readonly property real s: Math.min(width / Math.max(1, sourceW), height / Math.max(1, sourceH))
    readonly property real ox: (width - sourceW * s) / 2
    readonly property real oy: (height - sourceH * s) / 2
    default property alias overlays: layer.data

    radius: 24
    color: Theme.viewer
    clip: true

    function toImage(x, y) { return Qt.point((x - ox) / s, (y - oy) / s) }
    function toItem(px, py) { return Qt.point(ox + px * s, oy + py * s) }

    Image {
        id: img
        x: view.ox; y: view.oy
        width: view.sourceW * view.s
        height: view.sourceH * view.s
        cache: false
        asynchronous: false
        smooth: true
        mipmap: true
    }
    Item { id: layer; anchors.fill: parent }
}
