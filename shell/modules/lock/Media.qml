import QtQuick
import QtQuick.Layouts
import Quickshell
import Caelestia.Components
import Caelestia.Config
import qs.components
import qs.components.controls
import qs.components.images
import qs.services

StyledClippingRect {
    id: root

    required property var lock

    implicitHeight: layout.implicitHeight + layout.anchors.margins * 2
    radius: Tokens.rounding.extraLarge
    glass: true // Applestia: inner glass instead of a flat surface

    FadeImage {
        anchors.fill: parent
        source: Players.getArtUrl(Players.active)

        asynchronous: true
        fillMode: Image.PreserveAspectCrop
        sourceSize: {
            const dpr = (QsWindow.window as QsWindow)?.devicePixelRatio ?? 1;
            return Qt.size(width * dpr, height * dpr);
        }

        layer.enabled: true
        opacity: status === Image.Ready ? 1 : 0

        StyledRect {
            anchors.fill: parent
            color: "black" // Applestia: neutral scrim over the artwork
            opacity: 0.45
        }

        Behavior on opacity {
            Anim {
                type: Anim.StandardExtraLarge
            }
        }
    }

    ColumnLayout {
        id: layout

        anchors.left: parent.left
        anchors.right: parent.right
        anchors.verticalCenter: parent.verticalCenter
        anchors.margins: Tokens.padding.extraLarge
        spacing: Tokens.spacing.extraSmall

        StyledText {
            Layout.fillWidth: true
            animate: true
            text: (Players.active?.trackTitle ?? qsTr("Nothing playing")) || qsTr("Unknown track")
            color: AppleTokens.textPrimary // Applestia: Now Playing title
            horizontalAlignment: Text.AlignHCenter
            font: AppleTokens.titleFont
            elide: Text.ElideRight
        }

        StyledText {
            Layout.fillWidth: true
            animate: true
            text: (Players.active?.trackArtist ?? qsTr("Try playing some music!")) || qsTr("Unknown artist")
            color: AppleTokens.textSecondary
            horizontalAlignment: Text.AlignHCenter
            font: Tokens.font.body.small
            elide: Text.ElideRight
        }

        ButtonRow {
            Layout.alignment: Qt.AlignHCenter
            Layout.topMargin: Tokens.spacing.medium

            spacing: Tokens.spacing.extraSmall

            IconButton {
                type: IconButton.Tonal
                icon: "skip_previous"
                isRound: true
                shapeMorph: true
                disabled: !Players.active?.canGoPrevious
                onClicked: Players.active?.previous()
            }

            IconButton {
                icon: Players.active?.isPlaying ? "pause" : "play_arrow"
                isRound: true
                shapeMorph: true
                checked: Players.active?.isPlaying ?? false
                disabled: !Players.active?.canTogglePlaying
                onClicked: Players.active?.togglePlaying()
                implicitWidth: implicitHeight + Tokens.padding.largeIncreased * 2
            }

            IconButton {
                type: IconButton.Tonal
                icon: "skip_next"
                isRound: true
                shapeMorph: true
                disabled: !Players.active?.canGoNext
                onClicked: Players.active?.next()
            }
        }
    }
}
