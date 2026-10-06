import QtQuick
import QtQuick.Layouts
import Caelestia.Config
import qs.components
import qs.services

Row {
    id: root

    enum Type {
        Filled,
        Tonal
    }

    property real horizontalPadding: Tokens.padding.medium
    property real verticalPadding: Tokens.padding.small
    property int type: SplitButton.Filled
    property bool disabled
    property bool menuOnTop
    property string fallbackIcon
    property string fallbackText
    property real minLeftWidth

    property alias menuItems: menu.items
    property alias active: menu.active
    property alias expanded: menu.expanded
    readonly property alias menu: menu
    readonly property alias iconLabel: iconLabel
    readonly property alias label: label
    readonly property alias stateLayer: stateLayer
    readonly property alias textRow: textRow
    readonly property alias expandBtn: expandBtn

    property color colour: type == SplitButton.Filled ? Colours.palette.m3primary : Colours.palette.m3secondaryContainer
    // Applestia: tonal split buttons are inner glass, so their label is a
    // standard Apple label colour, not an on-container Material colour.
    property color textColour: type == SplitButton.Filled ? Colours.palette.m3onPrimary : AppleTokens.textPrimary
    property color disabledColour: Qt.alpha(Colours.palette.m3onSurface, 0.1)
    property color disabledTextColour: Qt.alpha(Colours.palette.m3onSurface, 0.38)

    spacing: Math.floor(Tokens.spacing.extraSmall / 2)

    StyledRect {
        radius: implicitHeight / 2 * Math.min(1, Tokens.rounding.scale)
        topRightRadius: Tokens.rounding.medium / 2
        bottomRightRadius: Tokens.rounding.medium / 2
        // Applestia: tonal split buttons are inner glass
        glass: root.type === SplitButton.Tonal && !root.disabled
        nativeTinted: !root.disabled
        color: glass ? "transparent" : root.disabled ? root.disabledColour : root.colour

        implicitWidth: Math.max(root.minLeftWidth, textRow.implicitWidth + root.horizontalPadding * 2)
        implicitHeight: expandBtn.implicitHeight

        StateLayer {
            id: stateLayer

            topRightRadius: parent.topRightRadius
            bottomRightRadius: parent.bottomRightRadius
            color: root.textColour
            disabled: root.disabled
            onClicked: root.active?.clicked()
        }

        RowLayout {
            id: textRow

            anchors.centerIn: parent
            anchors.horizontalCenterOffset: Math.floor(root.verticalPadding / 4)
            spacing: Tokens.spacing.small

            MaterialIcon {
                id: iconLabel

                Layout.alignment: Qt.AlignVCenter
                animate: true
                text: root.active?.activeIcon ?? root.fallbackIcon
                color: root.disabled ? root.disabledTextColour : root.textColour
                fill: 1
            }

            StyledText {
                id: label

                Layout.alignment: Qt.AlignVCenter
                Layout.preferredWidth: implicitWidth
                animate: true
                text: root.active?.activeText ?? root.fallbackText
                color: root.disabled ? root.disabledTextColour : root.textColour
                clip: true

                Behavior on Layout.preferredWidth {
                    Anim {
                        type: Anim.Emphasized
                    }
                }
            }
        }
    }

    StyledRect {
        id: expandBtn

        property real rad: root.expanded ? implicitHeight / 2 * Math.min(1, Tokens.rounding.scale) : Tokens.rounding.medium / 2

        radius: implicitHeight / 2 * Math.min(1, Tokens.rounding.scale)
        topLeftRadius: rad
        bottomLeftRadius: rad
        glass: root.type === SplitButton.Tonal && !root.disabled
        nativeTinted: !root.disabled
        color: glass ? "transparent" : root.disabled ? root.disabledColour : root.colour

        implicitWidth: implicitHeight
        implicitHeight: expandIcon.implicitHeight + root.verticalPadding * 2

        // Applestia: the button hands over to the liquid pill (Menu.qml) as the
        // menu opens, and takes it back as the menu drains in
        opacity: 1 - Math.min(1, menu.morph * 4)

        // Applestia: the menu morphs out of / back into this button (Menu.qml).
        // Opening squashes it as the menu leaves; closing makes it swallow the
        // menu and bounce back into shape.
        Connections {
            function onExpandedChanged(): void {
                if (root.expanded) {
                    absorbAnim.stop();
                    releaseAnim.restart();
                } else {
                    releaseAnim.stop();
                    absorbAnim.restart();
                }
            }

            target: root
        }

        SequentialAnimation {
            id: releaseAnim

            NumberAnimation {
                target: expandBtn
                property: "scale"
                to: 0.94
                duration: 90
                easing.type: Easing.OutQuad
            }
            NumberAnimation {
                target: expandBtn
                property: "scale"
                to: 1
                duration: 420
                easing.type: Easing.OutBack
                easing.overshoot: 2.2
            }
        }

        SequentialAnimation {
            id: absorbAnim

            PauseAnimation {
                duration: 300 // the liquid has drained back into the pill (Menu close = 360 ms)
            }
            NumberAnimation {
                target: expandBtn
                property: "scale"
                to: 1.08
                duration: 100
                easing.type: Easing.OutQuad
            }
            NumberAnimation {
                target: expandBtn
                property: "scale"
                to: 1
                duration: 460
                easing.type: Easing.OutBack
                easing.overshoot: 2.4
            }
        }

        StateLayer {
            id: expandStateLayer

            rect.topLeftRadius: parent.topLeftRadius
            rect.bottomLeftRadius: parent.bottomLeftRadius
            color: root.textColour
            disabled: root.disabled
            onClicked: root.expanded = !root.expanded
        }

        MaterialIcon {
            id: expandIcon

            anchors.centerIn: parent
            anchors.horizontalCenterOffset: root.expanded ? 0 : -Math.floor(root.verticalPadding / 4)

            text: "expand_more"
            color: root.disabled ? root.disabledTextColour : root.textColour
            rotation: root.expanded ? 180 : 0

            Behavior on anchors.horizontalCenterOffset {
                Anim {}
            }

            Behavior on rotation {
                Anim {}
            }
        }

        Behavior on rad {
            Anim {}
        }
    }

    Menu {
        id: menu

        attachTo: expandBtn
        // Applestia: the menu flows out of the button and covers it (aligned to
        // the button's own top/bottom edge, no gap), like Apple's button menus
        attachSideY: root.menuOnTop ? Menu.Bottom : Menu.Top
        thisSideY: root.menuOnTop ? Menu.Bottom : Menu.Top
    }
}
