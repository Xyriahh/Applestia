pragma Singleton

import QtQuick
import Quickshell
import qs.services

// Applestia design tokens. One place for sizes, type, glass tints
// and text colours so every Applestia component reads the same values.
Singleton {
    id: root

    readonly property bool light: Colours.light

    // ── Glass ───────────────────────────────────────────────────────────────
    // The tint IS the glass shape for hyprglass (silhouette threshold 0.04):
    // anything in a glass window at or above that alpha becomes glass.
    readonly property color glassTint: light ? Qt.rgba(1, 1, 1, 0.16) : Qt.rgba(1, 1, 1, 0.05)
    readonly property color glassTintHover: light ? Qt.rgba(1, 1, 1, 0.36) : Qt.rgba(1, 1, 1, 0.15)
    readonly property color glassTintPressed: light ? Qt.rgba(1, 1, 1, 0.44) : Qt.rgba(1, 1, 1, 0.20)
    readonly property color accent: Qt.hsla(Colours.palette.m3primary.hslHue, Colours.palette.m3primary.hslSaturation * 0.8, light ? 0.45 : 0.6, 1)
    readonly property color glassTintOn: Qt.alpha(accent, 0.55)

    // ── Text on glass: Apple's label colours (UIColor.label / secondaryLabel / tertiaryLabel)
    readonly property color textPrimary: light ? Qt.rgba(0, 0, 0, 0.88) : Qt.rgba(1, 1, 1, 0.95)
    readonly property color textSecondary: light ? Qt.rgba(60 / 255, 60 / 255, 67 / 255, 0.64) : Qt.rgba(235 / 255, 235 / 255, 245 / 255, 0.62)
    readonly property color textTertiary: light ? Qt.rgba(60 / 255, 60 / 255, 67 / 255, 0.32) : Qt.rgba(235 / 255, 235 / 255, 245 / 255, 0.32)
    // Apple's tertiarySystemFill: backgrounds of small pills/shapes on glass
    readonly property color fill: light ? Qt.rgba(118 / 255, 118 / 255, 128 / 255, 0.12) : Qt.rgba(118 / 255, 118 / 255, 128 / 255, 0.24)
    readonly property color separator: light ? Qt.rgba(60 / 255, 60 / 255, 67 / 255, 0.18) : Qt.rgba(84 / 255, 84 / 255, 88 / 255, 0.55)

    // ── Apple system colours (iOS / macOS, dark and light variants) ─────────
    // Used for meaning (calendar today, weather, activity rings, badges); the
    // wallpaper accent (Colours.palette) stays for selection and "on" states.
    readonly property color systemRed: light ? "#FF3B30" : "#FF453A"
    readonly property color systemOrange: light ? "#FF9500" : "#FF9F0A"
    readonly property color systemYellow: light ? "#FFCC00" : "#FFD60A"
    readonly property color systemGreen: light ? "#34C759" : "#30D158"
    readonly property color systemMint: light ? "#00C7BE" : "#63E6E2"
    readonly property color systemTeal: light ? "#30B0C7" : "#40C8E0"
    readonly property color systemCyan: light ? "#32ADE6" : "#64D2FF"
    readonly property color systemBlue: light ? "#007AFF" : "#0A84FF"
    readonly property color systemIndigo: light ? "#5856D6" : "#5E5CE6"
    readonly property color systemPurple: light ? "#AF52DE" : "#BF5AF2"
    readonly property color systemPink: light ? "#FF2D55" : "#FF375F"
    readonly property color systemGray: "#8E8E93"

    // ── Type ────────────────────────────────────────────────────────────────
    readonly property string textFamily: "SF Pro Text"
    readonly property string displayFamily: "SF Pro Display"
    readonly property font menuFont: Qt.font({ family: textFamily, pixelSize: 13, weight: Font.Normal, letterSpacing: -0.1, hintingPreference: Font.PreferNoHinting })
    readonly property font menuFontBold: Qt.font({ family: textFamily, pixelSize: 13, weight: Font.DemiBold, letterSpacing: -0.1, hintingPreference: Font.PreferNoHinting })
    readonly property font bodyFont: menuFont
    readonly property font captionFont: Qt.font({ family: textFamily, pixelSize: 11, weight: Font.Normal, hintingPreference: Font.PreferNoHinting })
    readonly property font titleFont: Qt.font({ family: textFamily, pixelSize: 15, weight: Font.DemiBold, hintingPreference: Font.PreferNoHinting })
    readonly property font headlineFont: Qt.font({ family: textFamily, pixelSize: 13, weight: Font.DemiBold, hintingPreference: Font.PreferNoHinting })
    readonly property font footnoteFont: Qt.font({ family: textFamily, pixelSize: 12, weight: Font.Normal, hintingPreference: Font.PreferNoHinting })
    // small uppercase label ("OCTOBER", "SUN"): semibold with open tracking
    readonly property font eyebrowFont: Qt.font({ family: textFamily, pixelSize: 11, weight: Font.DemiBold, letterSpacing: 0.6, capitalization: Font.AllUppercase, hintingPreference: Font.PreferNoHinting })

    // Large numerals (clock, temperature): SF Pro Display, tabular figures
    function displayFont(pixelSize: real, weight: int): font {
        return Qt.font({ family: displayFamily, pixelSize: pixelSize, weight: weight, letterSpacing: pixelSize >= 28 ? -0.6 : -0.3, features: { "tnum": 1 }, hintingPreference: Font.PreferNoHinting });
    }

    // ── Geometry (logical px) ───────────────────────────────────────────────
    readonly property int menuBarHeight: 36   // 26 px capsules + breathing room for the glass rim
    readonly property int capsuleHeight: 26
    readonly property int capsulePadding: 12
    readonly property int capsuleGap: 6
    readonly property int menuBarInset: 8
    readonly property int iconSize: 16
    readonly property int panelRadius: 26
    readonly property int moduleRadius: 14   // concentric: panel radius - panel padding (12)
    readonly property int menuRadius: 14
    readonly property int panelPadding: 12
    readonly property int popoverGap: 6

    // Multicolour-symbol colouring for Caelestia's weather glyphs (SF Symbols style)
    function weatherColour(icon: string): color {
        switch (icon) {
        case "clear_day":
        case "thunderstorm":
            return systemYellow;
        case "rainy":
            return systemCyan;
        case "foggy":
            return systemGray;
        default:
            return light ? Qt.rgba(0.55, 0.57, 0.62, 1) : Qt.rgba(1, 1, 1, 0.95); // clouds, snow
        }
    }

    // "org.gnome.Nautilus" -> "Nautilus", "zen" -> "Zen"
    function prettyWindowTitle(title: string): string {
        // The compositor's private material key is not a user-facing label.
        return title.replace(/^Applestia Settings \[nexus-[^\]]+\]/, "Settings");
    }

    function prettyAppName(cls: string): string {
        if (!cls)
            return "";
        const last = cls.split(".").pop().replace(/[-_]/g, " ");
        return last.charAt(0).toUpperCase() + last.slice(1);
    }
}
