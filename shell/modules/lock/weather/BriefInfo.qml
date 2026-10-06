import QtQuick
import QtQuick.Layouts
import Caelestia.Config
import qs.components
import qs.services

ColumnLayout {
    id: root

    required property int rootHeight

    spacing: Tokens.spacing.extraSmall

    StyledText {
        Layout.alignment: Qt.AlignHCenter
        animate: true
        text: Weather.description
        color: AppleTokens.textSecondary
        font: Tokens.font.body.large
    }

    RowLayout {
        Layout.alignment: Qt.AlignHCenter
        spacing: Tokens.spacing.medium

        StyledText {
            id: temp

            animate: true
            text: Weather.temp
            color: AppleTokens.textPrimary // Applestia: Weather-app temperature
            font: AppleTokens.displayFont(44, Font.Light)
        }

        MaterialIcon {
            animate: true
            text: Weather.icon
            color: AppleTokens.weatherColour(Weather.icon)
            fill: 1
            fontStyle: Tokens.font.headline.builders.large.scale(1.5).build()
        }
    }

    StyledText {
        visible: root.rootHeight > Tokens.sizes.lock.showWeatherDetailsHeight
        Layout.alignment: Qt.AlignHCenter
        animate: true
        text: qsTr("Feels like %1").arg(Weather.temp)
        color: AppleTokens.textSecondary
        font: Tokens.font.body.large
    }

    StyledText {
        visible: root.rootHeight > Tokens.sizes.lock.showWeatherDetailsHeight
        Layout.alignment: Qt.AlignHCenter
        animate: true
        text: {
            const today = Weather.forecast[0];
            return qsTr("High %1 • Low %2").arg(Weather.formatTemp(today?.maxTempC)).arg(Weather.formatTemp(today?.minTempC));
        }
        color: AppleTokens.textSecondary
        font: Tokens.font.body.medium
    }
}
