pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Effects
import M3Shapes
import Caelestia.Config
import qs.components
import qs.components.controls
import qs.components.effects
import qs.components.images
import qs.services

Item {
    id: root

    readonly property alias shape: shape

    property bool hadPrevious
    property color fallbackColour: AppleTokens.light ? Qt.rgba(0.82, 0.82, 0.85, 1) : Qt.rgba(0.23, 0.23, 0.25, 1) // Applestia: Apple artwork placeholder grey

    // Slight glow to separate from bg
    layer.enabled: true
    layer.effect: MultiEffect {
        shadowEnabled: true
        blurMax: 1
        shadowColor: Colours.palette.m3outline
        shadowOpacity: 0.3
    }

    Behavior on fallbackColour {
        CAnim {}
    }

    Item {
        id: shapeWrapper

        anchors.fill: parent
        layer.enabled: true
        opacity: root.fallbackColour.a

        MaterialShape {
            id: shape

            implicitSize: root.width
            shape: MaterialShape.Square // Applestia: rounded-square artwork, like Apple Music
            color: Qt.alpha(root.fallbackColour, 1)

            Anim on rotation {
                running: true
                paused: !Players.active?.isPlaying
                from: 360
                to: 0
                duration: 23500
                easing.type: Easing.Linear
                loops: Animation.Infinite
            }
        }
    }

    MaterialIcon {
        anchors.centerIn: parent

                text: image.status === Image.Error ? "broken_image" : "music_note"
        color: AppleTokens.textTertiary
        fill: 1
        fontStyle: Tokens.font.icon.size((parent.width * 0.35) || 1).build()
        opacity: image.status === Image.Null || image.status === Image.Error ? 1 : 0
        animate: true

        Behavior on opacity {
            Anim {
                type: Anim.DefaultEffects
            }
        }
    }

    Loader {
        anchors.centerIn: parent
        asynchronous: true
        active: opacity > 0
        opacity: image.status === Image.Loading ? 1 : 0

        sourceComponent: LoadingIndicator {
            implicitSize: root.width * 0.3
            color: AppleTokens.textTertiary
        }

        Behavior on opacity {
            Anim {
                type: Anim.DefaultEffects
            }
        }
    }

    FadeImage {
        id: image

        anchors.fill: parent

        source: Players.getArtUrl(Players.active)

        layer.enabled: true
        layer.effect: Mask {
            maskSource: shapeWrapper
        }
    }
}
