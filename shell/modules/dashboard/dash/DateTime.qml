pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts
import Caelestia.Config
import qs.components
import qs.services

// Applestia: Caelestia's vertical clock, set like an Apple widget: a red
// weekday eyebrow, hour and minutes in SF Pro Display Light, a hairline
// between them instead of the "•••".
Item {
    id: root

    anchors.top: parent.top
    anchors.bottom: parent.bottom
    implicitWidth: Tokens.sizes.dashboard.dateTimeWidth

    ColumnLayout {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.verticalCenter: parent.verticalCenter
        spacing: 2

        StyledText {
            Layout.alignment: Qt.AlignHCenter
            Layout.bottomMargin: 4
            text: Time.format("ddd")
            color: AppleTokens.systemRed
            font: AppleTokens.eyebrowFont
        }

        StyledText {
            Layout.alignment: Qt.AlignHCenter
            text: Time.hourStr
            color: AppleTokens.textPrimary
            font: AppleTokens.displayFont(34, Font.Light)
        }

        Rectangle {
            Layout.alignment: Qt.AlignHCenter
            implicitWidth: 18
            implicitHeight: 1
            color: AppleTokens.separator
        }

        StyledText {
            Layout.alignment: Qt.AlignHCenter
            text: Time.minuteStr
            color: AppleTokens.textPrimary
            font: AppleTokens.displayFont(34, Font.Light)
        }

        Loader {
            asynchronous: true
            Layout.alignment: Qt.AlignHCenter

            active: GlobalConfig.services.useTwelveHourClock
            visible: active

            sourceComponent: StyledText {
                text: Time.amPmStr
                color: AppleTokens.textSecondary
                font: AppleTokens.headlineFont
            }
        }
    }
}
