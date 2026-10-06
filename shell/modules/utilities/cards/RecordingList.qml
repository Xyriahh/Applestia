pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts
import Quickshell
import Quickshell.Widgets
import Caelestia.Config
import Caelestia.Models
import qs.components
import qs.components.containers
import qs.components.controls
import qs.services
import qs.utils

ColumnLayout {
    id: root

    required property var props
    required property ScreenState screenState

    spacing: 0

    WrapperMouseArea {
        Layout.fillWidth: true

        cursorShape: Qt.PointingHandCursor
        onClicked: root.props.recordingListExpanded = !root.props.recordingListExpanded

        RowLayout {
            spacing: Tokens.spacing.medium

            MaterialIcon {
                Layout.alignment: Qt.AlignVCenter
                text: "video_library"
                color: AppleTokens.textSecondary
                fill: 1
                fontStyle: Tokens.font.icon.large
            }

            StyledText {
                Layout.alignment: Qt.AlignVCenter
                Layout.fillWidth: true
                text: qsTr("Recordings")
                color: AppleTokens.textPrimary
                font: AppleTokens.headlineFont
            }

            IconButton {
                icon: root.props.recordingListExpanded ? "keyboard_arrow_up" : "keyboard_arrow_down"
                type: IconButton.Text
                inactiveOnColour: AppleTokens.textSecondary
                label.animate: true
                onClicked: root.props.recordingListExpanded = !root.props.recordingListExpanded
            }
        }
    }

    StyledListView {
        id: list

        model: FileSystemModel {
            path: Paths.recsdir
            nameFilters: ["recording_*.mp4"]
            sortReverse: true
        }

        Layout.fillWidth: true
        Layout.rightMargin: -Tokens.spacing.small
        implicitHeight: (Tokens.font.body.large.pointSize + Tokens.padding.small) * (root.props.recordingListExpanded ? 10 : 3)
        clip: true

        StyledScrollBar.vertical: StyledScrollBar {
            flickable: list
        }

        delegate: StyledRect {
            id: recording
            refractive: false // A hover highlight belongs to the recording list.

            required property FileSystemEntry modelData
            property string baseName

            anchors.left: list.contentItem.left
            anchors.right: list.contentItem.right
            anchors.rightMargin: Tokens.spacing.small

            implicitHeight: rowLayout.implicitHeight + Tokens.padding.extraSmall
            radius: Tokens.rounding.medium
            // Applestia: soft glass row highlight on hover
            color: rowHover.hovered ? AppleTokens.fill : "transparent"

            Behavior on color {
                CAnim {}
            }

            HoverHandler {
                id: rowHover
            }

            Component.onCompleted: baseName = modelData.baseName

            // Drag the label out of the panel to drop the file into any app that
            // accepts files (Discord, browsers, file managers, chat clients...).
            Drag.active: false
            Drag.dragType: Drag.Automatic
            Drag.supportedActions: Qt.CopyAction
            Drag.proposedAction: Qt.CopyAction
            Drag.mimeData: {
                const uri = "file://" + encodeURI(modelData.path).replace(/#/g, "%23");
                return {
                    "text/uri-list": uri + "\r\n",
                    "text/plain": modelData.path
                };
            }
            Drag.onDragFinished: {
                recording.Drag.active = false;
                root.props.recordingDragActive = false;
                // Drop landed (or was cancelled) elsewhere -- get out of the way,
                // same as the play/open buttons do.
                root.screenState.utilities = false;
                root.screenState.sidebar = false;
            }

            RowLayout {
                id: rowLayout

                anchors.left: parent.left
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                anchors.leftMargin: Tokens.padding.small
                anchors.rightMargin: Tokens.spacing.extraSmall
                spacing: Tokens.spacing.extraSmall

                MouseArea {
                    id: dragArea

                    property point pressPos

                    Layout.fillWidth: true
                    Layout.rightMargin: Tokens.spacing.extraSmall
                    implicitHeight: label.implicitHeight

                    cursorShape: pressed ? Qt.ClosedHandCursor : Qt.OpenHandCursor
                    // Keep the enclosing ListView from stealing the press as a flick
                    preventStealing: true

                    onPressed: event => {
                        pressPos = Qt.point(event.x, event.y);
                        recording.grabToImage(result => recording.Drag.imageSource = result.url);
                    }
                    onPositionChanged: event => {
                        if (!pressed || recording.Drag.active)
                            return;
                        const dx = event.x - pressPos.x;
                        const dy = event.y - pressPos.y;
                        if (Math.sqrt(dx * dx + dy * dy) < Qt.styleHints.startDragDistance)
                            return;
                        root.props.recordingDragActive = true;
                        recording.Drag.active = true;
                    }

                    StyledText {
                        id: label

                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.verticalCenter: parent.verticalCenter
                        text: {
                            const time = recording.baseName;
                            const matches = time.match(/^recording_(\d{4})(\d{2})(\d{2})_(\d{2})-(\d{2})-(\d{2})/);
                            if (!matches)
                                return time;
                            const date = new Date(...matches.slice(1));
                            date.setMonth(date.getMonth() - 1); // Woe (months start from 0)
                            return qsTr("Recording at %1").arg(Qt.formatDateTime(date, Qt.locale()));
                        }
                        color: AppleTokens.textPrimary
                        font: AppleTokens.footnoteFont
                        elide: Text.ElideRight
                    }
                }

                IconButton {
                    icon: "play_arrow"
                    type: IconButton.Text
                    inactiveOnColour: AppleTokens.textSecondary
                    onClicked: {
                        root.screenState.utilities = false;
                        root.screenState.sidebar = false;
                        Quickshell.execDetached([...GlobalConfig.general.apps.playback, recording.modelData.path]);
                    }
                }

                IconButton {
                    icon: "folder"
                    type: IconButton.Text
                    inactiveOnColour: AppleTokens.textSecondary
                    onClicked: {
                        root.screenState.utilities = false;
                        root.screenState.sidebar = false;
                        Quickshell.execDetached([...GlobalConfig.general.apps.explorer, recording.modelData.path]);
                    }
                }

                IconButton {
                    icon: "delete"
                    type: IconButton.Text
                    label.color: AppleTokens.systemRed
                    stateLayer.color: AppleTokens.systemRed
                    onClicked: root.props.recordingConfirmDelete = recording.modelData.path
                }
            }
        }

        add: Transition {
            Anim {
                type: Anim.DefaultEffects
                property: "opacity"
                from: 0
                to: 1
            }
        }

        remove: Transition {
            Anim {
                type: Anim.DefaultEffects
                property: "opacity"
                to: 0
            }
        }

        displaced: Transition {
            Anim {
                type: Anim.DefaultEffects
                property: "opacity"
                to: 1
            }
            Anim {
                property: "y"
            }
        }

        Loader {
            asynchronous: true
            anchors.centerIn: parent

            opacity: list.count === 0 ? 1 : 0
            active: opacity > 0

            sourceComponent: ColumnLayout {
                spacing: Tokens.spacing.small

                MaterialIcon {
                    Layout.alignment: Qt.AlignHCenter
                    text: "videocam_off"
                    color: AppleTokens.textTertiary
                    fontStyle: Tokens.font.icon.extraLarge

                    opacity: root.props.recordingListExpanded ? 1 : 0
                    scale: root.props.recordingListExpanded ? 1 : 0
                    Layout.preferredHeight: root.props.recordingListExpanded ? implicitHeight : 0

                    Behavior on opacity {
                        Anim {
                            type: Anim.DefaultEffects
                        }
                    }

                    Behavior on scale {
                        Anim {}
                    }

                    Behavior on Layout.preferredHeight {
                        Anim {}
                    }
                }

                RowLayout {
                    spacing: Tokens.spacing.medium

                    MaterialIcon {
                        Layout.alignment: Qt.AlignHCenter
                        text: "videocam_off"
                        color: AppleTokens.textTertiary

                        opacity: !root.props.recordingListExpanded ? 1 : 0
                        scale: !root.props.recordingListExpanded ? 1 : 0
                        Layout.preferredWidth: !root.props.recordingListExpanded ? implicitWidth : 0

                        Behavior on opacity {
                            Anim {
                                type: Anim.DefaultEffects
                            }
                        }

                        Behavior on scale {
                            Anim {}
                        }

                        Behavior on Layout.preferredWidth {
                            Anim {}
                        }
                    }

                    StyledText {
                        text: qsTr("No recordings found")
                        color: AppleTokens.textTertiary
                        font: AppleTokens.footnoteFont
                    }
                }
            }

            Behavior on opacity {
                Anim {
                    type: Anim.DefaultEffects
                }
            }
        }

        Behavior on implicitHeight {
            Anim {}
        }
    }
}
