import QtQuick
import Applestia.Glass

// The menu is visually reparented out of its triggering card. Two zero-area
// material-owner scopes retain its explicit card -> control -> menu stack depth
// (depth 2) without drawing phantom shapes or changing the wire contract.
Item {
    id: root
    required property var menuOwner
    parent: menuOwner.parent
    z: menuOwner.z
    visible: menuOwner.visible
    opacity: menuOwner.nativeBodyMix * menuOwner.bodyFade

    LiquidGlass {
        width: 0
        height: 0
        LiquidGlass {
            width: 0
            height: 0
            LiquidGlass {
                objectName: "nativeMenuBody"
                x: root.menuOwner.bodyCx - root.menuOwner.bodyW / 2
                y: root.menuOwner._up ? root.menuOwner.bodyEdge - root.menuOwner.bodyH : root.menuOwner.bodyEdge
                width: root.menuOwner.bodyW
                height: root.menuOwner.bodyH
                radius: root.menuOwner.bodyRadius
                tint: root.menuOwner.bodyTint
                preset: "applestia_control"
            }
        }
    }
}
