import QtQuick
import Caelestia.Config
import qs.components
import qs.services

// Applestia: Apple weather widget styling: multicolour symbol, temperature in
// SF Pro Display Light, condition as a secondary label.
Item {
    id: root

    anchors.centerIn: parent

    implicitWidth: icon.implicitWidth + info.implicitWidth + info.anchors.leftMargin
    implicitHeight: Math.max(icon.implicitHeight, info.implicitHeight) + Tokens.padding.largeIncreased * 2

    Component.onCompleted: Weather.reload()

    Item {
        id: icon

        anchors.verticalCenter: parent.verticalCenter
        anchors.left: parent.left
        implicitWidth: glyph.implicitWidth
        implicitHeight: glyph.implicitHeight

        // partly cloudy: a yellow sun peeking out behind a white cloud
        MaterialIcon {
            visible: Weather.icon === "partly_cloudy_day"
            x: glyph.implicitWidth * 0.30
            y: -glyph.implicitHeight * 0.12
            text: "clear_day"
            color: AppleTokens.systemYellow
            fill: 1
            fontStyle: Tokens.font.icon.builders.extraLarge.scale(1.05).build()
        }

        MaterialIcon {
            id: glyph

            animate: true
            text: Weather.icon === "partly_cloudy_day" ? "cloud" : Weather.icon
            color: Weather.icon === "partly_cloudy_day" ? AppleTokens.weatherColour("cloud") : AppleTokens.weatherColour(Weather.icon)
            fill: 1
            fontStyle: Tokens.font.icon.builders.extraLarge.scale(1.6).build()
        }
    }

    Column {
        id: info

        anchors.verticalCenter: parent.verticalCenter
        anchors.left: icon.right
        anchors.leftMargin: Tokens.spacing.largeIncreased

        spacing: 0

        StyledText {
            animate: true
            text: Weather.temp
            color: AppleTokens.textPrimary
            font: AppleTokens.displayFont(36, Font.Light)
        }

        StyledText {
            animate: true
            text: Weather.description
            color: AppleTokens.textSecondary
            font: AppleTokens.headlineFont

            elide: Text.ElideRight
            width: Math.min(implicitWidth, root.parent.width - icon.implicitWidth - info.anchors.leftMargin - Tokens.padding.extraLargeIncreased)
        }
    }
}
