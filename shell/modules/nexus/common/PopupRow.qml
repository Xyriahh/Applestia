import QtQuick
import QtQuick.Layouts
import Quickshell
import Caelestia.Config
import qs.components
import qs.services
import qs.modules.drawers
import qs.modules.nexus.common

ConnectedRect {
    id: root

    property alias icon: icon.text
    property alias label: label.text
    property alias status: status.text
    property bool keepPopupAsChild
    readonly property alias popup: popup
    default required property Item content

    Layout.fillWidth: true
    implicitHeight: navLayout.implicitHeight + navLayout.anchors.margins * 2

    StateLayer {
        id: stateLayer

        manualHoverOverride: popup.hovered && !popup.open
        onClicked: popup.open = true
    }

    RowLayout {
        id: navLayout

        anchors.fill: parent
        anchors.margins: Tokens.padding.medium
        anchors.leftMargin: Tokens.padding.largeIncreased
        anchors.rightMargin: Tokens.padding.largeIncreased
        spacing: Tokens.spacing.medium

        MaterialIcon {
            id: icon

            color: AppleTokens.textSecondary
            fontStyle: Tokens.font.icon.medium
        }

        ColumnLayout {
            Layout.fillWidth: true
            spacing: 0

            StyledText {
                id: label

                Layout.fillWidth: true
                color: AppleTokens.textPrimary
                font: AppleTokens.bodyFont
                elide: Text.ElideRight
            }

            StyledText {
                id: status

                Layout.fillWidth: true
                visible: text
                color: AppleTokens.textSecondary
                font: AppleTokens.footnoteFont
                elide: Text.ElideRight
                animate: true
            }
        }

        Item {
            id: triggerArea

            implicitWidth: popup.implicitWidth
            implicitHeight: popup.implicitHeight

            TransformWatcher {
                id: tWatcher

                a: area.parent
                b: triggerArea
            }

            MouseArea {
                id: area

                parent: {
                    if (root.keepPopupAsChild)
                        return triggerArea;

                    const win = QsWindow.window;
                    const contentWin = win as ContentWindow; // If inside the drawer content window, put it inside the interaction wrapper so hover works
                    return contentWin ? contentWin.interactionWrapper : (win as QsWindow).contentItem;
                }
                anchors.fill: parent
                enabled: popup.open
                hoverEnabled: popup.open
                cursorShape: popup.open ? Qt.ArrowCursor : undefined
                z: popup.animDriver > 0 ? 1 : 0

                onClicked: popup.open = false

                BlobPopup {
                    id: popup

                    x: {
                        tWatcher.transform;
                        return triggerArea.mapToItem(area.parent, 0, 0).x;
                    }
                    y: {
                        tWatcher.transform;
                        return triggerArea.mapToItem(area.parent, 0, 0).y;
                    }
                    padding: Tokens.padding.small
                    content: root.content
                    pressOverride: stateLayer.pressed
                    hoverOverride: stateLayer.containsMouse
                    // Applestia: glass popup; accent tint while open
                    color: open || hovered || stateLayer.containsMouse ? Qt.alpha(AppleTokens.accent, 0.55) : AppleTokens.fill
                }
            }
        }
    }
}
