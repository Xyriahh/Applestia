import QtQuick
import QtQuick.Layouts
import Quickshell
import Caelestia.Config
import qs.components
import qs.services

Item {
    id: root

    readonly property var usage: Usage.data

    implicitWidth: layout.implicitWidth > 900 ? layout.implicitWidth : 900
    implicitHeight: layout.implicitHeight

    Component.onCompleted: Usage.refCount++
    Component.onDestruction: Usage.refCount--

    ColumnLayout {
        id: layout

        anchors.fill: parent
        spacing: Tokens.spacing.medium

        // Header
        RowLayout {
            Layout.leftMargin: Tokens.padding.large
            Layout.rightMargin: Tokens.padding.large
            Layout.fillWidth: true

            Column {
                spacing: Tokens.spacing.extraSmall

                StyledText {
                    text: qsTr("AI Usage")
                    font: Tokens.font.body.builders.large.size(28).weight(Font.DemiBold).build()
                    color: AppleTokens.textPrimary
                }

                StyledText {
                    text: Usage.loading ? qsTr("Refreshing...") : qsTr("Provider limits & credits")
                    font: Tokens.font.body.small
                    color: AppleTokens.textSecondary
                }
            }

            Item {
                Layout.fillWidth: true
            }

            Item {
                Layout.alignment: Qt.AlignVCenter

                implicitWidth: refreshIcon.implicitWidth + Tokens.padding.medium
                implicitHeight: implicitWidth

                StateLayer {
                    anchors.fill: parent

                    radius: Tokens.rounding.full
                    color: Colours.palette.m3primary
                    onClicked: Usage.reload()
                }

                MaterialIcon {
                    id: refreshIcon

                    anchors.centerIn: parent
                    text: "update"
                    color: Colours.palette.m3primary
                    fontStyle: Tokens.font.icon.large
                    rotation: Usage.loading ? 360 : 0

                    Behavior on rotation {
                        Anim {
                            duration: 1000
                        }
                    }
                }
            }
        }

        // Loading / error state
        StyledText {
            Layout.alignment: Qt.AlignHCenter
            Layout.topMargin: Tokens.spacing.extraLarge
            visible: !root.usage
            text: Usage.error ? qsTr("Failed to load usage data") : qsTr("Loading usage data...")
            font: Tokens.font.body.medium
            color: AppleTokens.textSecondary
        }

        // OpenAI accounts
        ColumnLayout {
            Layout.fillWidth: true
            visible: root.usage

            spacing: Tokens.spacing.medium

            StyledText {
                Layout.leftMargin: Tokens.padding.medium
                text: qsTr("OpenAI")
                font: Tokens.font.body.builders.medium.weight(Font.DemiBold).build()
                color: AppleTokens.textPrimary
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: Tokens.spacing.medium

                Repeater {
                    model: ScriptModel {
                        values: (root.usage?.openai ?? []).filter(a => a.active) // Applestia: only the active OpenAI account
                    }

                    delegate: StyledRect {
                        id: accountCard

                        required property var modelData

                        Layout.fillWidth: true
                        implicitHeight: accountLayout.implicitHeight + Tokens.padding.large * 2

                        radius: Tokens.rounding.large
                        glass: true // Applestia: inner glass instead of a flat surface

                        readonly property bool limited: modelData.limit_reached || modelData.error

                        ColumnLayout {
                            id: accountLayout

                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.verticalCenter: parent.verticalCenter
                            spacing: Tokens.spacing.small

                            RowLayout {
                                Layout.leftMargin: Tokens.padding.large
                                Layout.rightMargin: Tokens.padding.large
                                Layout.fillWidth: true
                                spacing: Tokens.spacing.small

                                MaterialIcon {
                                    text: "bolt"
                                    fill: 1
                                    color: accountCard.limited ? AppleTokens.systemRed : Colours.palette.m3primary
                                    fontStyle: Tokens.font.icon.builders.medium.weight(Font.DemiBold).build()
                                }

                                ColumnLayout {
                                    spacing: 0

                                    StyledText {
                                        Layout.fillWidth: true
                                        text: accountCard.modelData.label ?? qsTr("OpenAI")
                                        font: Tokens.font.body.builders.medium.weight(Font.DemiBold).build()
                                        elide: Text.ElideRight
                                    }

                                    StyledText {
                                        text: accountCard.modelData.plan ?? ""
                                        font: Tokens.font.body.small
                                        opacity: 0.7
                                        color: AppleTokens.textSecondary
                                    }
                                }

                                Item {
                                    Layout.fillWidth: true
                                }

                                StyledText {
                                    visible: !!accountCard.modelData.active
                                    text: qsTr("Active")
                                    font: Tokens.font.body.small
                                    color: Colours.palette.m3primary
                                }

                                MaterialIcon {
                                    visible: !!accountCard.modelData.active
                                    text: "check_circle"
                                    fill: 1
                                    color: Colours.palette.m3primary
                                    fontStyle: Tokens.font.icon.medium
                                }
                            }

                            StyledText {
                                Layout.leftMargin: Tokens.padding.large
                                visible: !!accountCard.modelData.error
                                text: qsTr("Unavailable: ") + (accountCard.modelData.error ?? "")
                                font: Tokens.font.body.small
                                color: AppleTokens.systemRed
                                elide: Text.ElideRight
                            }

                            Repeater {
                                model: ScriptModel {
                                    values: accountCard.modelData.windows ?? []
                                }

                                delegate: ColumnLayout {
                                    id: windowColumn

                                    required property var modelData

                                    Layout.leftMargin: Tokens.padding.large
                                    Layout.rightMargin: Tokens.padding.large
                                    Layout.fillWidth: true
                                    spacing: Tokens.spacing.extraSmall

                                    RowLayout {
                                        Layout.fillWidth: true

                                        StyledText {
                                            text: windowColumn.modelData.label ?? ""
                                            font: Tokens.font.body.small
                                            color: AppleTokens.textSecondary
                                        }

                                        Item {
                                            Layout.fillWidth: true
                                        }

                                        StyledText {
                                            readonly property real remaining: windowColumn.modelData.remaining ?? 0

                                            text: qsTr("%1% left").arg(Math.round(remaining))
                                            font: Tokens.font.body.builders.small.weight(Font.DemiBold).build()
                                            color: remaining <= 0 ? AppleTokens.systemRed : (remaining <= 15 ? AppleTokens.systemOrange : AppleTokens.textPrimary)
                                        }

                                        StyledText {
                                            visible: !!(windowColumn.modelData.reset_in ?? "")
                                            text: qsTr("resets in ") + (windowColumn.modelData.reset_in ?? "")
                                            font: Tokens.font.body.small
                                            opacity: 0.7
                                            color: AppleTokens.textSecondary
                                        }
                                    }

                                    StyledRect {
                                        Layout.fillWidth: true

                                        implicitHeight: 8
                                        radius: Tokens.rounding.full
                                        color: Colours.palette.m3outlineVariant

                                        StyledRect {
                                            anchors.left: parent.left
                                            anchors.top: parent.top
                                            anchors.bottom: parent.bottom

                                            readonly property real fraction: Math.max(0, Math.min(100, windowColumn.modelData.remaining ?? 0)) / 100

                                            width: parent.width * fraction
                                            radius: parent.radius
                                            color: fraction <= 0 ? AppleTokens.systemRed : (fraction <= 0.15 ? AppleTokens.systemOrange : Colours.palette.m3primary)

                                            Behavior on width {
                                                Anim {}
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }

        // Claude
        ColumnLayout {
            Layout.fillWidth: true
            visible: root.usage

            spacing: Tokens.spacing.medium

            StyledText {
                Layout.leftMargin: Tokens.padding.medium
                text: qsTr("Claude")
                font: Tokens.font.body.builders.medium.weight(Font.DemiBold).build()
                color: AppleTokens.textPrimary
            }

            StyledRect {
                Layout.fillWidth: true

                implicitHeight: claudeLayout.implicitHeight + Tokens.padding.large * 2

                radius: Tokens.rounding.large
                glass: true // Applestia: inner glass instead of a flat surface

                ColumnLayout {
                    id: claudeLayout

                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: Tokens.spacing.small

                    RowLayout {
                        Layout.leftMargin: Tokens.padding.large
                        Layout.rightMargin: Tokens.padding.large
                        Layout.fillWidth: true
                        spacing: Tokens.spacing.small

                        MaterialIcon {
                            text: "psychology"
                            fill: 1
                            color: AppleTokens.systemTeal
                            fontStyle: Tokens.font.icon.builders.medium.weight(Font.DemiBold).build()
                        }

                        StyledText {
                            text: root.usage?.claude?.plan ?? "Claude"
                            font: Tokens.font.body.builders.medium.weight(Font.DemiBold).build()
                        }

                        Item {
                            Layout.fillWidth: true
                        }

                        StyledText {
                            visible: !!root.usage?.claude?.error
                            text: qsTr("Unavailable: ") + (root.usage?.claude?.error ?? "")
                            font: Tokens.font.body.small
                            color: AppleTokens.systemRed
                            elide: Text.ElideRight
                        }
                    }

                    Repeater {
                        model: ScriptModel {
                            values: root.usage?.claude?.windows ?? []
                        }

                        delegate: ColumnLayout {
                            id: claudeWindow

                            required property var modelData

                            Layout.leftMargin: Tokens.padding.large
                            Layout.rightMargin: Tokens.padding.large
                            Layout.fillWidth: true
                            spacing: Tokens.spacing.extraSmall

                            RowLayout {
                                Layout.fillWidth: true

                                StyledText {
                                    text: claudeWindow.modelData.label ?? ""
                                    font: Tokens.font.body.small
                                    color: AppleTokens.textSecondary
                                }

                                Item {
                                    Layout.fillWidth: true
                                }

                                StyledText {
                                    visible: claudeWindow.modelData.remaining === null || claudeWindow.modelData.remaining === undefined
                                    text: claudeWindow.modelData.tokens ?? ""
                                    font: Tokens.font.body.builders.small.weight(Font.DemiBold).build()
                                }

                                StyledText {
                                    readonly property real remaining: claudeWindow.modelData.remaining ?? 0

                                    visible: claudeWindow.modelData.remaining !== null && claudeWindow.modelData.remaining !== undefined
                                    text: qsTr("%1% left").arg(Math.round(remaining))
                                    font: Tokens.font.body.builders.small.weight(Font.DemiBold).build()
                                    color: remaining <= 0 ? AppleTokens.systemRed : (remaining <= 15 ? AppleTokens.systemOrange : AppleTokens.textPrimary)
                                }

                                StyledText {
                                    visible: !!(claudeWindow.modelData.reset_in ?? "")
                                    text: qsTr("resets in ") + (claudeWindow.modelData.reset_in ?? "")
                                    font: Tokens.font.body.small
                                    opacity: 0.7
                                    color: AppleTokens.textSecondary
                                }
                            }

                            StyledRect {
                                visible: claudeWindow.modelData.remaining !== null && claudeWindow.modelData.remaining !== undefined
                                Layout.fillWidth: true

                                implicitHeight: 8
                                radius: Tokens.rounding.full
                                color: Colours.palette.m3outlineVariant

                                StyledRect {
                                    anchors.left: parent.left
                                    anchors.top: parent.top
                                    anchors.bottom: parent.bottom

                                    readonly property real fraction: Math.max(0, Math.min(100, claudeWindow.modelData.remaining ?? 0)) / 100

                                    width: parent.width * fraction
                                    radius: parent.radius
                                    color: fraction <= 0 ? AppleTokens.systemRed : (fraction <= 0.15 ? AppleTokens.systemOrange : AppleTokens.systemTeal)

                                    Behavior on width {
                                        Anim {}
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }

        // API credits
        ColumnLayout {
            Layout.fillWidth: true
            visible: root.usage

            spacing: Tokens.spacing.medium

            StyledText {
                Layout.leftMargin: Tokens.padding.medium
                text: qsTr("API credits")
                font: Tokens.font.body.builders.medium.weight(Font.DemiBold).build()
                color: AppleTokens.textPrimary
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: Tokens.spacing.medium

                CreditCard {
                    Layout.fillWidth: true

                    icon: "savings"
                    label: qsTr("DeepSeek")
                    colour: AppleTokens.systemOrange
                    value: root.usage?.deepseek?.error ? qsTr("Unavailable") : (root.usage?.deepseek?.balance ?? "--")
                }

                CreditCard {
                    Layout.fillWidth: true

                    icon: "electric_bolt"
                    label: qsTr("NeuralWatt")
                    colour: Colours.palette.m3primary
                    value: root.usage?.neuralwatt?.error ? qsTr("Unavailable") : (root.usage?.neuralwatt?.balance ?? "--")
                    subtext: {
                        if (root.usage?.neuralwatt?.error)
                            return "";
                        let text = root.usage?.neuralwatt?.total ? qsTr("of ") + root.usage.neuralwatt.total : "";
                        const kwh = root.usage?.neuralwatt?.kwh_remaining;
                        if (typeof kwh === "number")
                            text += `${text ? " \u00b7 " : ""}${kwh} kWh ${qsTr("left")}`;
                        return text;
                    }
                }
            }
        }
    }

    component CreditCard: StyledRect {
        id: creditRoot

        property string icon
        property string label
        property string value
        property string subtext
        property color colour

        implicitHeight: creditLayout.implicitHeight + Tokens.padding.large * 2

        radius: Tokens.rounding.large
        glass: true // Applestia: inner glass instead of a flat surface

        Row {
            id: creditLayout

            anchors.centerIn: parent
            spacing: Tokens.spacing.medium

            MaterialIcon {
                anchors.verticalCenter: parent.verticalCenter
                text: creditRoot.icon
                fill: 1
                color: creditRoot.colour
                fontStyle: Tokens.font.icon.large
            }

            Column {
                anchors.verticalCenter: parent.verticalCenter
                spacing: 0

                StyledText {
                    text: creditRoot.label
                    font: Tokens.font.body.small
                    opacity: 0.7
                    color: AppleTokens.textSecondary
                }

                StyledText {
                    text: creditRoot.value
                    font: Tokens.font.body.builders.medium.weight(Font.DemiBold).build()
                    color: creditRoot.colour
                }

                StyledText {
                    visible: !!creditRoot.subtext
                    text: creditRoot.subtext
                    font: Tokens.font.body.small
                    opacity: 0.7
                    color: AppleTokens.textSecondary
                }
            }
        }
    }
}
