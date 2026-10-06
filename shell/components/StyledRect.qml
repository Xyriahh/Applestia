import QtQuick
import qs.components.glass

// Non-rendering material owner. Public color remains the caller's reactive
// binding; only the backing Rectangle changes its rendering on native glass.
Item {
    id: root

    property color color: "transparent"
    property alias radius: backing.radius
    property alias topLeftRadius: backing.topLeftRadius
    property alias topRightRadius: backing.topRightRadius
    property alias bottomRightRadius: backing.bottomRightRadius
    property alias bottomLeftRadius: backing.bottomLeftRadius
    property alias border: backing.border
    property alias antialiasing: backing.antialiasing
    property var gradient: null
    property bool glass
    property real glassFrost: 1
    property real lensWidth: 0
    property bool lensEnabled: true
    property bool refractive: glass || color.a >= 0.04
    // Opt coloured selections/controls in explicitly; ordinary badges stay solid.
    property bool nativeTinted: false
    property string glassPreset: "applestia_control"
    property color nativeTint: color.a > 0 ? Qt.alpha(color, Math.min(color.a, 0.55)) : InnerGlass.innerGlassTint
    readonly property bool nativeGlass: enabled && (glass || nativeTinted) && refractive && lensEnabled && InnerGlass.nativeEligible(root)

    Component.onCompleted: InnerGlass.registerSurface(root)
    Component.onDestruction: InnerGlass.unregisterSurface(root)

    Behavior on color { CAnim {} }

    Rectangle {
        id: backing
        anchors.fill: parent
        z: -1
        color: root.nativeGlass ? "transparent" : root.color
        gradient: root.nativeGlass ? null : root.gradient
    }

    Loader {
        anchors.fill: parent
        z: -2
        active: root.glass && !root.nativeGlass
        asynchronous: false
        sourceComponent: GlassFill {
            radius: root.radius
            topLeftRadius: root.topLeftRadius
            topRightRadius: root.topRightRadius
            bottomRightRadius: root.bottomRightRadius
            bottomLeftRadius: root.bottomLeftRadius
            frost: root.glassFrost
        }
    }

    Loader {
        anchors.fill: parent
        active: root.nativeGlass
        // Loader defaults active:true. A wrapper created after capability is
        // available may never emit activeChanged; initialize that case too.
        Component.onCompleted: {
            if (active && source.toString() === "") setSource("glass/NativeMaterial.qml", { owner: root });
        }
        onActiveChanged: {
            if (active) setSource("glass/NativeMaterial.qml", { owner: root });
            else source = "";
        }
    }
}
