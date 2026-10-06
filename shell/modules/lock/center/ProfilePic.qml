pragma ComponentBehavior: Bound

import QtQuick
import M3Shapes
import Caelestia.Config
import qs.components
import qs.components.effects
import qs.components.images
import qs.services
import qs.utils

Item {
    id: root

    required property int centerWidth
    // Applestia: macOS login-style avatar: smaller, neutral translucent disc
    readonly property color bgColour: Qt.rgba(1, 1, 1, 0.18)

    implicitWidth: Math.round(centerWidth * 0.36)
    implicitHeight: {
        shape.height; // Force update when shape height changes
        return shape.pathBounds().height;
    }

    MaterialShape {
        id: shape

        anchors.centerIn: parent
        implicitSize: root.implicitWidth

        shape: MaterialShape.Circle
        color: Qt.alpha(root.bgColour, 1)
        opacity: root.bgColour.a
        layer.enabled: true
    }

    MaterialIcon {
        anchors.centerIn: parent

        text: "person"
        color: Qt.rgba(1, 1, 1, 0.85)
        fill: 1
        fontStyle: Tokens.font.icon.size(root.implicitWidth * 0.5).build()
        visible: pfp.status !== Image.Ready
    }

    CachingImage {
        id: pfp

        anchors.fill: shape
        path: `${Paths.home}/.face`

        layer.enabled: true
        layer.effect: Mask {
            maskSource: shape
        }
    }
}
