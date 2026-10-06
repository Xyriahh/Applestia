import QtQuick
import QtQuick.Layouts
import Caelestia.Config
import Caelestia.Services
import qs.components
import qs.components.controls
import qs.services

Item {
    id: root

    anchors.top: parent.top
    anchors.bottom: parent.bottom

    implicitWidth: layout.implicitWidth + layout.anchors.margins * 2

    ServiceRef {
        service: Cpu
    }

    ServiceRef {
        service: Memory
    }

    ServiceRef {
        service: Storage
    }

    ColumnLayout {
        id: layout

        anchors.horizontalCenter: parent.horizontalCenter
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        anchors.margins: Tokens.padding.large
        spacing: Tokens.spacing.medium

        // Applestia: Activity-ring style in the wallpaper's accent family
        Resource {
            icon: "memory"
            value: Cpu.percentage
            fgColour: Colours.palette.m3primary
        }

        Resource {
            icon: "memory_alt"
            value: Memory.percentage
            fgColour: Colours.palette.m3tertiary
        }

        Resource {
            icon: "hard_disk"
            value: Storage.percentage
            fgColour: Colours.palette.m3secondary
        }
    }
    component Resource: CircularProgress {
        id: res

        required property string icon

        Layout.fillHeight: true
        implicitSize: height
        strokeWidth: Tokens.sizes.dashboard.resourceProgressThickness + 2
        bgColour: Qt.alpha(fgColour, 0.22) // a dim track in the ring's own colour

        Behavior on clampedVal {
            Anim {}
        }

        MaterialIcon {
            anchors.centerIn: parent
            text: res.icon
            fontStyle: Tokens.font.icon.medium
            fill: 1
            color: res.fgColour
        }
    }
}
