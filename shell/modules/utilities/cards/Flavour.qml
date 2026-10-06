import QtQuick
import QtQuick.Layouts
import Quickshell
import Quickshell.Io
import Caelestia.Components
import Caelestia.Config
import qs.components
import qs.components.controls
import qs.services

// Applestia: switch between the stock Caelestia shell and the Liquid Glass flavour.
// The work is done by ~/.local/bin/applestia-switch.
StyledRect {
    id: root

    readonly property string stateDir: `${Quickshell.env("HOME")}/.local/state/applestia`
    property string mode: "caelestia"
    property bool approved: false
    property bool switching: false

    function switchTo(target: string): void {
        if (target === mode || switching)
            return;
        switching = true;
        Quickshell.execDetached(["applestia-switch", target]);
    }

    implicitHeight: layout.implicitHeight + Tokens.padding.extraLargeIncreased

    radius: Tokens.rounding.large
    glass: true // Applestia: inner glass instead of a flat surface

    FileView {
        path: `${root.stateDir}/mode`
        watchChanges: true
        printErrors: false
        onFileChanged: reload()
        onLoaded: root.mode = text().trim() === "applestia" ? "applestia" : "caelestia"
    }

    FileView {
        path: `${root.stateDir}/plugin-approved`
        watchChanges: true
        printErrors: false
        onFileChanged: reload()
        onLoaded: root.approved = true
        onLoadFailed: root.approved = false
    }

    ColumnLayout {
        id: layout

        anchors.fill: parent
        anchors.margins: Tokens.padding.large
        spacing: Tokens.spacing.medium

        RowLayout {
            Layout.fillWidth: true
            spacing: Tokens.spacing.medium

            // Applestia: Settings-style badge for the shell style card
            StyledRect {
                implicitWidth: 36
                implicitHeight: 36
                Layout.alignment: Qt.AlignVCenter

                radius: Tokens.rounding.medium
                color: AppleTokens.systemIndigo

                MaterialIcon {
                    anchors.centerIn: parent
                    text: "palette"
                    color: "white"
                    fill: 1
                    fontStyle: Tokens.font.icon.medium
                }
            }

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 0

                StyledText {
                    Layout.fillWidth: true
                    text: qsTr("Shell style")
                    color: AppleTokens.textPrimary
                    font: AppleTokens.headlineFont
                    elide: Text.ElideRight
                }

                StyledText {
                    Layout.fillWidth: true
                    text: root.switching ? qsTr("Switching…") : root.approved ? qsTr("Restarts the shell") : qsTr("Applestia: glass plugin not approved yet")
                    color: AppleTokens.textSecondary
                    font: AppleTokens.footnoteFont
                    elide: Text.ElideRight
                }
            }
        }

        // Applestia: Apple segmented control (glass track + sliding pill),
        // replaces the two Material tonal buttons.
        Item {
            id: segmented

            Layout.fillWidth: true
            implicitHeight: 30

            readonly property int currentIndex: root.mode === "applestia" ? 1 : 0

            StyledRect {
                anchors.fill: parent
                radius: height / 2
                glass: true
                glassFrost: 1.3
            }

            Item {
                id: indicator

                width: parent.width / 2
                height: parent.height
                x: width * segmented.currentIndex

                StyledRect {
                    anchors.fill: parent
                    anchors.margins: 2
                    radius: height / 2
                    color: AppleTokens.light ? Qt.rgba(1, 1, 1, 0.92) : Qt.rgba(1, 1, 1, 0.16)
                    glass: true
                    glassFrost: 0.6
                }

                Behavior on x {
                    Anim {}
                }
            }

            Row {
                anchors.fill: parent

                Repeater {
                    model: [
                        {
                            label: "Caelestia",
                            enabled: true
                        },
                        {
                            label: "Applestia",
                            enabled: root.approved
                        }
                    ]

                    delegate: Item {
                        required property int index
                        required property var modelData

                        readonly property bool current: segmented.currentIndex === index

                        width: segmented.width / 2
                        height: segmented.height

                        StyledText {
                            anchors.centerIn: parent
                            text: parent.modelData.label
                            color: !parent.modelData.enabled ? AppleTokens.textTertiary : parent.current ? AppleTokens.textPrimary : AppleTokens.textSecondary
                            font: parent.current ? AppleTokens.menuFontBold : AppleTokens.menuFont

                            Behavior on color {
                                CAnim {}
                            }
                        }

                        StateLayer {
                            anchors.fill: parent
                            radius: height / 2
                            color: AppleTokens.textPrimary
                            disabled: !parent.modelData.enabled
                            showHoverBackground: !parent.current
                            onClicked: root.switchTo(index === 0 ? "caelestia" : "applestia")
                        }
                    }
                }
            }
        }
    }
}
