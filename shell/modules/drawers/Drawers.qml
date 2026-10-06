pragma ComponentBehavior: Bound

import QtQuick
import Quickshell
import Quickshell.Wayland
import Caelestia.Config
import qs.components
import qs.components.containers
import qs.services

Variants {
    model: Screens.screens

    Scope {
        id: scope

        required property ShellScreen modelData

        Exclusions {
            screen: scope.modelData
            bar: content.bar
        }

        // Applestia: the dimming scrim lives in its own window (excluded from
        // glass) below the drawers; inside the glass window it would turn into glass.
        StyledWindow {
            readonly property ScreenState screenState: ShellState.forScreen(scope.modelData)
            readonly property bool shown: screenState.session && Config.session.enabled

            screen: scope.modelData
            name: "scrim"
            anchors.top: true
            anchors.bottom: true
            anchors.left: true
            anchors.right: true
            exclusionMode: ExclusionMode.Ignore
            WlrLayershell.layer: WlrLayer.Top
            mask: Region {}
            visible: scrim.opacity > 0

            Rectangle {
                id: scrim

                anchors.fill: parent
                color: "black"
                opacity: parent.shown ? 0.35 : 0

                Behavior on opacity {
                    Anim {
                        type: Anim.SlowEffects
                    }
                }
            }
        }

        ContentWindow {
            id: content

            screen: scope.modelData
        }
    }
}
