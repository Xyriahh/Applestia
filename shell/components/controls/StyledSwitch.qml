import QtQuick
import QtQuick.Templates
import Caelestia.Config
import qs.components
import qs.components.glass
import qs.services

// Applestia: iOS-style switch. Accent track when on, inner-glass track when
// off; a white glass knob that stretches while held and springs across.
Switch {
    id: root

    property int cLayer: 1 // kept for API compatibility with Caelestia call sites
    property bool disabled

    enabled: !disabled
    opacity: disabled ? 0.45 : 1

    implicitWidth: implicitIndicatorWidth
    implicitHeight: implicitIndicatorHeight

    indicator: StyledRect {
        id: track

        implicitHeight: Math.round(Tokens.font.body.medium.pointSize + Tokens.padding.small * 2)
        implicitWidth: Math.round(implicitHeight * 1.65)
        radius: height / 2
        color: root.checked ? Colours.palette.m3primary : "transparent"
        glass: !root.checked
        refractive: true
        nativeTinted: true
        glassFrost: 1.6

        StyledRect {
            id: knob

            readonly property real inset: 2
            readonly property real baseSize: track.height - inset * 2

            anchors.verticalCenter: parent.verticalCenter
            height: baseSize
            width: root.pressed ? baseSize * 1.28 : baseSize
            radius: height / 2
            x: root.checked ? track.width - width - inset : inset
            color: "white"
            nativeTinted: true

            // glass rim on the knob so it reads as a lens, not a flat disc
            GlassFill {
                visible: !knob.nativeGlass
                anchors.fill: parent
                radius: parent.radius
                tint: Qt.rgba(1, 1, 1, 0)
                rimStrength: 0.9
                sheen: 0.0
            }

            // soft contact shadow under the knob (kept faint: it sits on glass)
            Rectangle {
                visible: !knob.nativeGlass
                anchors.fill: parent
                anchors.topMargin: 1.5
                anchors.bottomMargin: -1.5
                z: -1
                radius: parent.radius
                color: Qt.rgba(0, 0, 0, 0.18)
            }

            Behavior on x {
                NumberAnimation {
                    duration: 420
                    easing.type: Easing.OutBack
                    easing.overshoot: 1.6
                }
            }

            Behavior on width {
                NumberAnimation {
                    duration: root.pressed ? 140 : 380
                    easing.type: root.pressed ? Easing.OutQuad : Easing.OutBack
                    easing.overshoot: 2
                }
            }
        }

        Behavior on color {
            CAnim {}
        }
    }
}
