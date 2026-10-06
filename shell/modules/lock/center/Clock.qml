pragma ComponentBehavior: Bound

import QtQuick
import Caelestia.Config
import qs.components
import qs.services

// Applestia: iOS lock-screen clock. One SF Pro Display numeral run in a
// single translucent white (no two-colour split), tabular figures so the
// width doesn't jump, a small AM/PM label when using 12-hour time.
Item {
    id: root

    required property real centerScale

    readonly property real size: 132 * centerScale

    implicitWidth: time.implicitWidth + (ampm.visible ? ampm.implicitWidth + 8 : 0)
    implicitHeight: metrics.tightBoundingRect.height

    TextMetrics {
        id: metrics

        text: time.text
        font: time.font
    }

    StyledText {
        id: time

        y: -(metrics.tightBoundingRect.y - metrics.boundingRect.y)
        text: `${Time.hourStr}:${Time.minuteStr}`
        color: Qt.rgba(1, 1, 1, 0.92)
        font: AppleTokens.displayFont(root.size, Font.DemiBold)
    }

    StyledText {
        id: ampm

        anchors.left: time.right
        anchors.leftMargin: 8
        anchors.bottom: parent.bottom
        visible: GlobalConfig.services.useTwelveHourClock
        text: Time.amPmStr
        color: Qt.rgba(1, 1, 1, 0.75)
        font: AppleTokens.displayFont(28 * root.centerScale, Font.DemiBold)
    }
}
