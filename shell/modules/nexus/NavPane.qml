import "navpane"
import QtQuick
import QtQuick.Layouts
import Caelestia.Config
import qs.components
import qs.components.controls
import qs.services
import qs.modules.nexus

ColumnLayout {
    id: root

    required property NexusState nState

    spacing: Tokens.spacing.large

    SearchBar {
        id: searchField

        Layout.fillWidth: true

        placeholderText: qsTr("Search settings")
        font: AppleTokens.bodyFont

        searchIcon.color: AppleTokens.textSecondary
        searchIcon.fontStyle: Tokens.font.icon.medium
        searchIcon.anchors.leftMargin: Tokens.padding.largeIncreased
        clearIcon.font: Tokens.font.icon.medium
        clearIcon.padding: Tokens.padding.extraSmall
        clearIcon.inactiveOnColour: AppleTokens.textSecondary

        Binding {
            target: root.nState
            property: "searchOpen"
            value: searchField.text.length > 0
        }
    }

    NavLocations {
        Layout.fillWidth: true
        Layout.fillHeight: true
        Layout.topMargin: -topMargin
        Layout.bottomMargin: -bottomMargin
        nState: root.nState
    }
}
