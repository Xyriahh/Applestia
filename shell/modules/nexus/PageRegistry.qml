pragma Singleton

import QtQuick
import qs.components

QtObject {
    id: root

    readonly property list<var> pages: [
        // Appearance
        {
            label: qsTr("Wallpaper & style"),
            icon: "palette",
            description: qsTr("Wallpaper, fonts, colours"),
            category: "appearance",
            colour: AppleTokens.systemIndigo // Applestia: Settings-style badge colour
        },

        // Connectivity
        // TODO
        // {
        //     label: qsTr("Display"),
        //     icon: "monitor",
        //     description: qsTr("Output configuration"),
        //     category: "connectivity"
        // },
        {
            label: qsTr("Network"),
            icon: "wifi",
            description: qsTr("Wi-Fi, ethernet, VPN"),
            category: "connectivity",
            colour: AppleTokens.systemBlue
        },
        {
            label: qsTr("Connected devices"),
            icon: "devices_other",
            description: qsTr("Bluetooth, pairing"),
            category: "connectivity",
            noFill: true,
            colour: AppleTokens.systemCyan
        },
        {
            label: qsTr("Audio"),
            icon: "volume_up",
            description: qsTr("App volumes, sound devices"),
            category: "connectivity",
            colour: AppleTokens.systemPink
        },

        // System
        {
            label: qsTr("Updates"),
            icon: "update",
            description: qsTr("System updates"),
            category: "system",
            colour: AppleTokens.systemGreen
        },
        {
            label: qsTr("Plugins"),
            icon: "extension",
            description: qsTr("Manage plugins"),
            category: "system",
            colour: AppleTokens.systemPurple
        },

        // Shell
        {
            label: qsTr("Panels"),
            icon: "dock_to_bottom",
            description: qsTr("Dashboard, taskbar, launcher, sidebar"),
            category: "shell",
            colour: AppleTokens.systemTeal
        },
        {
            label: qsTr("Apps"),
            icon: "apps",
            description: qsTr("Default apps, favourites, hidden apps"),
            category: "shell",
            colour: AppleTokens.systemOrange
        },
        {
            label: qsTr("Services"),
            icon: "build",
            description: qsTr("Poll intervals, lyrics backend"),
            category: "shell",
            colour: AppleTokens.systemGray
        },
        {
            label: qsTr("Language & region"),
            icon: "globe",
            description: qsTr("UI language, weather location, display units"),
            category: "shell",
            colour: AppleTokens.systemMint
        },

        // About
        {
            label: qsTr("About"),
            icon: "info",
            description: qsTr("System information, credits"),
            category: "about",
            colour: AppleTokens.systemGray
        },
    ]
}
