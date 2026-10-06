import QtQuick
import QtQuick.Layouts
import Caelestia
import Caelestia.Config
import qs.components
import qs.components.effects
import qs.services

StyledRect {
    id: root

    required property Toast modelData

    readonly property color accentColour: {
        if (root.modelData.type === Toast.Success)
            return AppleTokens.systemGreen;
        if (root.modelData.type === Toast.Warning)
            return AppleTokens.systemOrange;
        if (root.modelData.type === Toast.Error)
            return AppleTokens.systemRed;
        return AppleTokens.systemBlue;
    }

    anchors.left: parent.left
    anchors.right: parent.right
    implicitHeight: layout.implicitHeight + Tokens.padding.large

    radius: Tokens.rounding.large
    glass: true // Applestia: inner glass instead of a flat surface

    Elevation {
        anchors.fill: parent
        radius: parent.radius
        opacity: parent.opacity
        z: -1
        level: 3
    }

    RowLayout {
        id: layout

        anchors.fill: parent
        anchors.margins: Tokens.padding.small
        anchors.leftMargin: Tokens.padding.medium
        anchors.rightMargin: Tokens.padding.medium
        spacing: Tokens.spacing.medium

        // Applestia: Settings-style badge in the toast's system colour
        StyledRect {
            radius: Tokens.rounding.medium
            color: root.accentColour

            implicitWidth: implicitHeight
            implicitHeight: icon.implicitHeight + Tokens.padding.large

            MaterialIcon {
                id: icon

                anchors.centerIn: parent
                text: root.modelData.icon
                color: "white"
                fill: 1
                fontStyle: Tokens.font.icon.builders.large.scale(1.2).build()
            }
        }

        ColumnLayout {
            Layout.fillWidth: true
            spacing: 0

            StyledText {
                id: title

                Layout.fillWidth: true
                text: root.modelData.title
                color: AppleTokens.textPrimary
                font: AppleTokens.headlineFont
                elide: Text.ElideRight
            }

            StyledText {
                Layout.fillWidth: true
                textFormat: Text.StyledText
                text: root.modelData.message
                color: AppleTokens.textSecondary
                font: AppleTokens.footnoteFont
                elide: Text.ElideRight
            }
        }
    }
}
