pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import Quickshell
import Quickshell.Widgets
import Caelestia.Config
import qs.components
import qs.services

StackView {
    id: root

    required property PopoutState popouts
    required property QsMenuHandle trayItem

    implicitWidth: currentItem?.implicitWidth ?? 0
    implicitHeight: currentItem?.implicitHeight ?? 0

    initialItem: SubMenu {
        handle: root.trayItem
    }

    // Applestia: iOS-style navigation: a submenu slides in from the right while
    // its parent eases left; Back reverses it
    pushEnter: NavAnim {
        fromX: root.width * 0.35
        fromOpacity: 0
        toOpacity: 1
    }
    pushExit: NavAnim {
        toX: -root.width * 0.18
        fromOpacity: 1
        toOpacity: 0
    }
    popEnter: NavAnim {
        fromX: -root.width * 0.18
        fromOpacity: 0
        toOpacity: 1
    }
    popExit: NavAnim {
        toX: root.width * 0.35
        fromOpacity: 1
        toOpacity: 0
    }

    component NavAnim: Transition {
        property real fromX
        property real toX
        property real fromOpacity
        property real toOpacity

        ParallelAnimation {
            Anim {
                property: "x"
                from: fromX
                to: toX
                type: Anim.DefaultSpatial
            }
            Anim {
                property: "opacity"
                from: fromOpacity
                to: toOpacity
                type: Anim.DefaultEffects
            }
        }
    }

    Component {
        id: subMenuComp

        SubMenu {}
    }

    component SubMenu: Column {
        id: menu

        required property QsMenuHandle handle
        property bool isSubMenu
        property bool shown

        padding: Tokens.padding.small
        spacing: Tokens.spacing.small

        opacity: shown ? 1 : 0

        Component.onCompleted: shown = true
        StackView.onActivating: shown = true
        StackView.onDeactivating: shown = false
        StackView.onRemoved: destroy()

        Behavior on opacity {
            Anim {
                type: Anim.DefaultEffects
            }
        }


        QsMenuOpener {
            id: menuOpener

            menu: menu.handle
        }

        Repeater {
            model: menuOpener.children

            StyledRect {
                id: item

                required property QsMenuEntry modelData

                implicitWidth: Tokens.sizes.bar.trayMenuWidth
                implicitHeight: modelData.isSeparator ? 1 : children.implicitHeight

                radius: Tokens.rounding.full
                color: modelData.isSeparator ? AppleTokens.separator : "transparent"

                Loader {
                    id: children

                    asynchronous: true
                    anchors.left: parent.left
                    anchors.right: parent.right

                    active: !item.modelData.isSeparator

                    sourceComponent: Item {
                        implicitHeight: label.implicitHeight

                        StateLayer {
                            anchors.margins: -Tokens.padding.extraSmall / 2
                            anchors.leftMargin: -Tokens.padding.small
                            anchors.rightMargin: -Tokens.padding.small

                            radius: item.radius
                            disabled: !item.modelData.enabled

                            onClicked: {
                                const entry = item.modelData;
                                if (entry.hasChildren)
                                    root.push(subMenuComp.createObject(null, {
                                        handle: entry,
                                        isSubMenu: true
                                    }));
                                else {
                                    item.modelData.triggered();
                                    root.popouts.hasCurrent = false;
                                }
                            }
                        }

                        Loader {
                            id: icon

                            asynchronous: true
                            anchors.left: parent.left

                            active: item.modelData.icon !== ""

                            sourceComponent: IconImage {
                                asynchronous: true
                                implicitSize: label.implicitHeight

                                source: item.modelData.icon
                            }
                        }

                        StyledText {
                            id: label

                            anchors.left: icon.right
                            anchors.leftMargin: icon.active ? Tokens.spacing.medium : 0

                            text: labelMetrics.elidedText
                            color: item.modelData.enabled ? AppleTokens.textPrimary : AppleTokens.textTertiary
                        }

                        TextMetrics {
                            id: labelMetrics

                            text: item.modelData.text
                            font: label.font

                            elide: Text.ElideRight
                            elideWidth: root.Tokens.sizes.bar.trayMenuWidth - (icon.active ? icon.implicitWidth + label.anchors.leftMargin : 0) - (expand.active ? expand.implicitWidth + root.Tokens.spacing.medium : 0)
                        }

                        Loader {
                            id: expand

                            asynchronous: true
                            anchors.verticalCenter: parent.verticalCenter
                            anchors.right: parent.right

                            active: item.modelData.hasChildren

                            sourceComponent: MaterialIcon {
                                text: "chevron_right"
                                color: item.modelData.enabled ? AppleTokens.textPrimary : AppleTokens.textTertiary
                            }
                        }
                    }
                }
            }
        }

        Loader {
            asynchronous: true
            active: menu.isSubMenu

            sourceComponent: Item {
                implicitWidth: back.implicitWidth
                implicitHeight: back.implicitHeight + Tokens.spacing.extraSmall

                Item {
                    anchors.bottom: parent.bottom
                    implicitWidth: back.implicitWidth
                    implicitHeight: back.implicitHeight

                    StyledRect {
                        anchors.fill: parent
                        anchors.margins: -Tokens.padding.extraSmall / 2
                        anchors.leftMargin: -Tokens.padding.small
                        anchors.rightMargin: -Tokens.padding.large

                        radius: Tokens.rounding.full
                        glass: true // Applestia: glass capsule

                        StateLayer {
                            radius: parent.radius
                            color: AppleTokens.textPrimary
                            onClicked: root.pop()
                        }
                    }

                    Row {
                        id: back

                        anchors.verticalCenter: parent.verticalCenter

                        MaterialIcon {
                            anchors.verticalCenter: parent.verticalCenter
                            text: "chevron_left"
                            color: AppleTokens.textPrimary
                        }

                        StyledText {
                            anchors.verticalCenter: parent.verticalCenter
                            text: qsTr("Back")
                            color: AppleTokens.textPrimary
                        }
                    }
                }
            }
        }
    }
}
