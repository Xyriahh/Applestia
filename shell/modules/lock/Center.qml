import "center"
import QtQuick
import QtQuick.Layouts
import Caelestia.Config
import qs.components
import qs.services

ColumnLayout {
    id: root

    required property var lock
    readonly property real centerScale: Math.min(1, (lock.screen?.height ?? 1440) / 1440)
    readonly property int centerWidth: Tokens.sizes.lock.centerWidth * centerScale

    Layout.preferredWidth: centerWidth
    Layout.fillWidth: false
    Layout.fillHeight: true

    spacing: Tokens.spacing.largeIncreased

    // Applestia: date above the clock, like iOS ("Monday 5 October")
    StyledText {
        Layout.alignment: Qt.AlignHCenter
        Layout.topMargin: Tokens.padding.large

        text: Time.format("dddd d MMMM")
        color: Qt.rgba(1, 1, 1, 0.85)
        font: AppleTokens.displayFont(22 * Math.max(0.8, root.centerScale), Font.DemiBold)
    }

    Clock {
        Layout.alignment: Qt.AlignHCenter
        Layout.topMargin: -Tokens.spacing.medium
        centerScale: root.centerScale
    }

    ProfilePic {
        Layout.alignment: Qt.AlignHCenter
        Layout.topMargin: Tokens.spacing.extraExtraLarge * root.centerScale
        Layout.bottomMargin: Tokens.spacing.extraLarge * root.centerScale
        centerWidth: root.centerWidth
    }

    PasswordInput {
        Layout.alignment: Qt.AlignHCenter
        centerScale: Math.max(0.8, root.centerScale)
        centerWidth: root.centerWidth
        lock: root.lock
    }

    StateMessage {
        Layout.fillWidth: true
        pam: root.lock.pam
    }
}
