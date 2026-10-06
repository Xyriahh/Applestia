pragma ComponentBehavior: Bound

import QtQuick
import M3Shapes
import Caelestia.Config
import qs.components
import qs.components.effects
import qs.components.filedialog
import qs.components.images
import qs.services
import qs.utils

Item {
    id: root

    required property ScreenState screenState
    required property FileDialog facePicker

    property color pfpFallbackColour: Colours.layer(Colours.palette.m3surfaceContainerHighest, 2)

    anchors.fill: parent
    anchors.margins: Tokens.padding.large

    Behavior on pfpFallbackColour {
        CAnim {}
    }

    Item {
        id: pfpContainer

        anchors.top: parent.top
        anchors.bottom: parent.bottom
        anchors.left: logoShape.right
        anchors.leftMargin: -(Tokens.padding.largeIncreased + Tokens.padding.extraLarge) / 2
        implicitWidth: height

        MaterialShape {
            id: shape

            anchors.centerIn: parent
            implicitSize: parent.height
            shape: MaterialShape.Circle // Applestia: round avatar, like Contacts
            color: Qt.alpha(root.pfpFallbackColour, 1)
            opacity: root.pfpFallbackColour.a
            layer.enabled: true

            MouseArea {
                id: mouse

                containmentMask: QtObject {
                    function contains(pt: point): bool {
                        return shape.contains(pt) && !logoShape.contains(mouse.mapToItem(logoShape, pt)) && !uptimeShape.contains(mouse.mapToItem(uptimeShape, pt));
                    }
                }

                anchors.fill: parent
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                onClicked: {
                    root.screenState.dashboard = false;
                    root.facePicker.open();
                }
            }
        }

        Item {
            anchors.fill: parent
            layer.enabled: true
            layer.effect: Mask {
                maskSource: shape
            }

            Loader {
                anchors.centerIn: parent
                asynchronous: true
                active: pfp.status !== Image.Ready

                sourceComponent: MaterialIcon {
                    text: "person_add"
                    color: Colours.palette.m3onSurfaceVariant
                    fontStyle: Tokens.font.icon.extraLarge
                    fill: 1
                    grade: -2 // Ugh material symbols are such a pain with fill
                }
            }

            CachingImage {
                id: pfp

                anchors.fill: parent
                path: `${Paths.home}/.face`
            }

            StyledRect {
                anchors.fill: parent
                color: Qt.alpha(Colours.palette.m3scrim, pfp.status === Image.Ready ? 0.4 : 0)
                opacity: mouse.containsMouse ? 1 : 0
                layer.enabled: opacity < 1

                Behavior on opacity {
                    Anim {
                        type: Anim.DefaultEffects
                    }
                }

                MaterialShape {
                    anchors.centerIn: parent
                    implicitSize: parent.height * 0.7
                    shape: MaterialShape.Circle
                    color: AppleTokens.systemBlue
                    scale: mouse.pressed ? 0.9 : mouse.containsMouse ? 1 : 0.7

                    Behavior on color {
                        CAnim {}
                    }

                    Behavior on scale {
                        Anim {
                            type: Anim.FastSpatial
                        }
                    }

                    MaterialIcon {
                        anchors.centerIn: parent
                        text: "person_edit"
                        color: Colours.palette.m3onPrimary
                        fontStyle: Tokens.font.icon.large
                    }
                }
            }
        }
    }

    MaterialShape {
        id: logoShape

        x: Tokens.padding.extraSmall
        implicitSize: Tokens.sizes.dashboard.logoSize + Tokens.padding.small * 2
        shape: MaterialShape.Square // Applestia: Settings-style badge
        color: AppleTokens.systemIndigo

        Behavior on color {
            CAnim {}
        }

        Loader {
            anchors.centerIn: parent
            sourceComponent: SysInfo.isDefaultLogo ? caelestiaLogo : osLogo
        }
    }

    Component {
        id: osLogo

        ColouredIcon {
            id: icon

            source: SysInfo.osLogo
            implicitSize: Tokens.sizes.dashboard.logoSize
            colour: "white"
        }
    }

    Component {
        id: caelestiaLogo

        Logo {
            implicitWidth: Tokens.sizes.dashboard.logoSize
            implicitHeight: Tokens.sizes.dashboard.logoSize
            topColour: "white"
            bottomColour: Qt.rgba(1, 1, 1, 0.75)
        }
    }

    MaterialShape {
        id: uptimeShape

        anchors.bottom: parent.bottom
        anchors.left: pfpContainer.right
        anchors.bottomMargin: 0 // Applestia: square badge, no clamshell compensation
        anchors.leftMargin: -Tokens.padding.extraLargeIncreased
        implicitSize: Tokens.sizes.dashboard.uptimeSize + Tokens.padding.small * 2
        shape: MaterialShape.Square // Applestia: Settings-style badge
        color: AppleTokens.systemOrange

        Behavior on color {
            CAnim {}
        }

        MaterialIcon {
            anchors.centerIn: parent
            text: "schedule"
            color: "white"
            fill: 1
            fontStyle: Tokens.font.icon.medium
        }
    }

    StyledText {
        anchors.left: uptimeShape.right
        anchors.verticalCenter: uptimeShape.verticalCenter
        anchors.leftMargin: Tokens.spacing.small
        anchors.verticalCenterOffset: Math.round(fontInfo.pointSize * 0.1)

        color: AppleTokens.textSecondary
        font: AppleTokens.headlineFont
        text: "Up " + SysInfo.uptime.split(",").slice(0, 2).join(",") // Max 2 components
        width: Tokens.sizes.dashboard.userWidth - x - Tokens.padding.extraLarge
        elide: Text.ElideRight
    }

    StyledRect {
        id: bubble1

        visible: false // Applestia: no thought bubbles

        anchors.left: pfpContainer.right
        anchors.top: bubble2.bottom
        anchors.leftMargin: Tokens.spacing.small
        anchors.topMargin: -Tokens.spacing.extraSmall

        implicitWidth: 10
        implicitHeight: 10
        radius: Tokens.rounding.full
        color: Colours.palette.m3secondaryContainer
    }

    StyledRect {
        id: bubble2

        visible: false

        anchors.left: bubble1.right
        anchors.verticalCenter: wmContainer.bottom
        anchors.leftMargin: Tokens.spacing.extraSmall

        implicitWidth: 15
        implicitHeight: 15
        radius: Tokens.rounding.full
        color: Colours.palette.m3secondaryContainer
    }

    StyledRect {
        id: wmContainer

        anchors.left: bubble2.left
        anchors.leftMargin: -Tokens.padding.medium
        y: Tokens.padding.extraSmall

        radius: height / 2
        glass: true // Applestia: glass capsule
        implicitWidth: wmLabel.implicitWidth + Tokens.padding.medium * 2
        implicitHeight: wmLabel.implicitHeight + Tokens.padding.small * 2

        Row {
            id: wmLabel

            anchors.centerIn: parent
            spacing: Tokens.spacing.extraSmall

            MaterialIcon {
                id: wmIcon

                anchors.verticalCenter: parent.verticalCenter
                text: "select_window"
                color: AppleTokens.textSecondary
                fontStyle: wmText.font
            }

            StyledText {
                id: wmText

                anchors.verticalCenter: parent.verticalCenter
                anchors.verticalCenterOffset: Math.round(fontInfo.pointSize * 0.1)
                text: SysInfo.wm
                color: AppleTokens.textPrimary
                font: AppleTokens.headlineFont
                width: Math.min(implicitWidth, Tokens.sizes.dashboard.userWidth - wmContainer.x - Tokens.padding.medium * 2 - wmIcon.implicitWidth - wmLabel.spacing - Tokens.padding.extraLarge)
                elide: Text.ElideRight
            }
        }
    }
}
