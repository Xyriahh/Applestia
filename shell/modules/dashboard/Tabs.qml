pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts
import QtQuick.Templates
import Quickshell
import Caelestia.Config
import qs.components
import qs.components.controls
import qs.services

Item {
    id: root

    required property real nonAnimWidth
    required property ScreenState screenState
    required property var tabs

    readonly property alias count: bar.count


    // Applestia: an Apple segmented control: one glass capsule track, a sliding
    // selection pill, glyph + label side by side (SF Pro Text), no underline.
    implicitHeight: bar.implicitHeight + bar.anchors.topMargin + Tokens.spacing.small

    StyledRect {
        id: track

        anchors.fill: bar
        anchors.margins: -3
        radius: height / 2
        glass: true
        glassFrost: 1.3
    }

    // Animate the tab INDEX, not a pixel x: the panel width animates when a
    // tab change resizes the dashboard, and an x derived from that width would
    // move, stall and move again. Position = animated index × current width.
    property real animIndex: bar.currentIndex

    Behavior on animIndex {
        Anim {}
    }

    Item {
        id: indicator

        readonly property real segment: bar.width / Math.max(1, bar.count)

        anchors.top: bar.top
        anchors.bottom: bar.bottom
        x: bar.x + segment * root.animIndex
        width: segment

        StyledRect {
            anchors.fill: parent
            radius: height / 2
            color: AppleTokens.light ? Qt.rgba(1, 1, 1, 0.92) : Qt.rgba(1, 1, 1, 0.16)
            glass: true
            glassFrost: 0.6

            // soft lift under the selected segment (light mode, like iOS)
            Rectangle {
                anchors.fill: parent
                anchors.topMargin: 1
                anchors.bottomMargin: -1
                z: -1
                radius: parent.radius
                color: Qt.rgba(0, 0, 0, AppleTokens.light ? 0.10 : 0)
            }
        }
    }

    TabBar {
        id: bar

        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.topMargin: Tokens.spacing.small

        currentIndex: root.screenState.dashboardTab
        onCurrentIndexChanged: root.screenState.dashboardTab = currentIndex

        implicitHeight: contentHeight
        background: null
        contentItem: RowLayout {
            spacing: 0

            Repeater {
                model: bar.contentModel
            }
        }

        Repeater {
            model: ScriptModel {
                values: root.tabs
            }

            delegate: Tab {
                required property var modelData

                iconName: modelData.iconName
                text: modelData.text
            }
        }
    }

    component Tab: TabButton {
        id: tab

        required property string iconName
        readonly property bool current: TabBar.tabBar.currentItem === this

        Layout.fillWidth: true
        Layout.preferredWidth: 1 // Uniform width across all tabs
        implicitWidth: implicitContentWidth
        implicitHeight: implicitContentHeight
        background: null

        contentItem: CustomMouseArea {
            id: mouse

            function onWheel(event: WheelEvent): void {
                if (event.angleDelta.y < 0)
                    root.screenState.dashboardTab = Math.min(root.screenState.dashboardTab + 1, bar.count - 1);
                else if (event.angleDelta.y > 0)
                    root.screenState.dashboardTab = Math.max(root.screenState.dashboardTab - 1, 0);
            }

            implicitWidth: icon.width + label.width + 6
            implicitHeight: Math.max(icon.height, label.height) + 12

            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor

            onPressed: root.screenState.dashboardTab = tab.TabBar.index

            StateLayer {
                id: stateLayer

                radius: height / 2
                color: AppleTokens.textPrimary
                showHoverBackground: !tab.current
                onClicked: root.screenState.dashboardTab = tab.TabBar.index
            }

            Row {
                anchors.centerIn: parent
                spacing: 6

                MaterialIcon {
                    id: icon

                    anchors.verticalCenter: parent.verticalCenter
                    text: tab.iconName
                    color: tab.current ? AppleTokens.textPrimary : AppleTokens.textSecondary
                    fill: tab.current ? 1 : 0
                    fontStyle: Tokens.font.icon.small

                    Behavior on fill {
                        Anim {
                            type: Anim.DefaultEffects
                        }
                    }
                }

                StyledText {
                    id: label

                    anchors.verticalCenter: parent.verticalCenter
                    text: tab.text
                    color: tab.current ? AppleTokens.textPrimary : AppleTokens.textSecondary
                    font: tab.current ? AppleTokens.menuFontBold : AppleTokens.menuFont
                }
            }
        }
    }
}
