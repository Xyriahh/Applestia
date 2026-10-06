import QtQuick
import Quickshell.Widgets
import qs.components.glass

// Keep Quickshell's actual rounded shader clip for foreground children. The
// material owner stays outside that texture and never hides/reparents labels.
Item {
    id: root

    default property alias data: backing.data
    property alias children: backing.children
    readonly property alias contentItem: backing.contentItem
    property alias contentInsideBorder: backing.contentInsideBorder
    property alias contentUnderBorder: backing.contentUnderBorder
    property alias radius: backing.radius
    property alias topLeftRadius: backing.topLeftRadius
    property alias topRightRadius: backing.topRightRadius
    property alias bottomRightRadius: backing.bottomRightRadius
    property alias bottomLeftRadius: backing.bottomLeftRadius
    property alias border: backing.border
    property alias antialiasing: backing.antialiasing
    property color color: "transparent"
    property bool glass
    property real glassFrost: 1
    property real lensWidth: 0
    property bool lensEnabled: true
    property bool refractive: glass || color.a >= 0.04
    property bool nativeTinted: false
    property string glassPreset: "applestia_control"
    property color nativeTint: color.a > 0 ? Qt.alpha(color, Math.min(color.a, 0.55)) : InnerGlass.innerGlassTint
    readonly property bool nativeGlass: enabled && (glass || nativeTinted) && refractive && lensEnabled && InnerGlass.nativeEligible(root)

    Component.onCompleted: InnerGlass.registerSurface(root)
    Component.onDestruction: InnerGlass.unregisterSurface(root)
    Behavior on color { CAnim {} }

    // Explicit property objects avoid sending implementation children through
    // our public default data/children aliases into the clipped content item.
    property Item _backing: ClippingRectangle {
        id: backing
        parent: root
        anchors.fill: parent
        color: root.nativeGlass ? "transparent" : root.color
    }
    property Item _fallback: Loader {
        parent: backing.contentItem
        anchors.fill: parent
        z: -1
        active: root.glass && !root.nativeGlass
        sourceComponent: GlassFill {
            radius: root.radius
            topLeftRadius: root.topLeftRadius
            topRightRadius: root.topRightRadius
            bottomRightRadius: root.bottomRightRadius
            bottomLeftRadius: root.bottomLeftRadius
            frost: root.glassFrost
        }
    }
    property Item _native: Loader {
        parent: root
        anchors.fill: parent
        active: root.nativeGlass
        // Newly loaded cards can start active without an activeChanged signal.
        Component.onCompleted: {
            if (active && source.toString() === "") setSource("glass/NativeMaterial.qml", { owner: root });
        }
        onActiveChanged: {
            if (active) setSource("glass/NativeMaterial.qml", { owner: root });
            else source = "";
        }
    }
}
