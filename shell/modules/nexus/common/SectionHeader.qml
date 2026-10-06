import QtQuick
import QtQuick.Layouts
import Caelestia.Config
import qs.components
import qs.services

StyledText {
    property bool first

    Layout.fillWidth: true
    Layout.topMargin: first ? 0 : Tokens.spacing.largeIncreased - ((parent as ColumnLayout).spacing ?? 0)
    Layout.bottomMargin: Tokens.spacing.extraSmall
    Layout.leftMargin: Tokens.padding.small

    // Applestia: Settings-style small uppercase section label
    color: AppleTokens.textTertiary
    font: AppleTokens.eyebrowFont
    elide: Text.ElideRight
}
