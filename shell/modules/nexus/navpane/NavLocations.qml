pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts
import Caelestia.Config
import qs.components
import qs.components.containers
import qs.services
import qs.modules.nexus

VerticalFadeFlickable {
    id: root

    required property NexusState nState

    topMargin: Tokens.padding.large
    bottomMargin: Tokens.padding.large
    contentHeight: content.implicitHeight

    TapHandler {
        onTapped: root.focus = true
    }

    ColumnLayout {
        id: content

        anchors.left: parent.left
        anchors.right: parent.right
        spacing: Tokens.spacing.extraSmall

        Repeater {
            id: list

            model: PageRegistry.pages

            StyledRect {
                id: item

                required property var modelData
                required property int index

                readonly property bool isCurrentPage: index === root.nState.currentPageIdx
                readonly property bool isCategoryStart: index === 0 || PageRegistry.pages[index - 1].category !== modelData.category

                Layout.fillWidth: true
                Layout.topMargin: index !== 0 && isCategoryStart ? Tokens.spacing.medium : 0
                implicitHeight: layout.implicitHeight + layout.anchors.margins * 2

                // Applestia: macOS sidebar selection (accent tint pill), no
                // category corner logic; hover is a soft fill.
                radius: Tokens.rounding.medium
                color: isCurrentPage ? Qt.alpha(AppleTokens.accent, 0.35) : "transparent"

                Behavior on color {
                    CAnim {}
                }

                StateLayer {
                    id: stateLayer

                    anchors.fill: parent
                    radius: parent.radius
                    color: AppleTokens.textPrimary
                    showHoverBackground: !item.isCurrentPage
                    onClicked: root.nState.currentPageIdx = item.index
                }

                RowLayout {
                    id: layout

                    anchors.fill: parent
                    anchors.margins: Tokens.padding.small
                    anchors.leftMargin: Tokens.padding.medium
                    spacing: Tokens.spacing.medium

                    // Applestia: Settings-style squircle badge per page
                    StyledRect {
                        Layout.alignment: Qt.AlignVCenter
                        implicitWidth: 26
                        implicitHeight: 26

                        radius: Tokens.rounding.small
                        color: item.modelData.colour ?? AppleTokens.systemGray

                        MaterialIcon {
                            anchors.centerIn: parent
                            anchors.verticalCenterOffset: 1

                            text: item.modelData.icon
                            color: "white"
                            fontStyle: Tokens.font.icon.builders.small.weight(Font.Medium).build()
                            grade: 25
                            fill: item.modelData.noFill ? 0 : 1
                        }
                    }

                    StyledText {
                        Layout.fillWidth: true
                        text: item.modelData.label
                        color: AppleTokens.textPrimary
                        font: AppleTokens.menuFont
                        elide: Text.ElideRight
                    }
                }
            }
        }
    }
}
