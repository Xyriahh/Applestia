import QtQuick
import QtTest
import ".."

TestCase {
    name: "NativeMenuMaterial"
    when: windowShown

    Component {
        id: materialComponent
        NativeMenuMaterial {}
    }

    Item {
        id: visualRoot
        width: 800
        height: 600
        Item {
            id: menu
            property real nativeBodyMix: 0.5
            property real bodyFade: 1
            property real bodyCx: 200
            property real bodyW: 160
            property real bodyH: 100
            property real bodyEdge: 70
            property real bodyRadius: 28
            property bool _up: false
            property color bodyTint: "#44101014"
        }
    }

    function test_bodyGeometryAndClosing() {
        const material = createTemporaryObject(materialComponent, visualRoot, { menuOwner: menu });
        // Required properties must be supplied at creation, so use a component
        // rather than assigning the owner after the native item is constructed.
        verify(material !== null);
        const body = findChild(material, "nativeMenuBody");
        verify(body !== null);
        compare(body.x, 120);
        compare(body.y, 70);
        compare(body.width, 160);
        compare(body.height, 100);
        compare(body.radius, 28);
        compare(body.preset, "applestia_control");
        compare(material.opacity, 0.5);
        compare(body.parent.width, 0);
        compare(body.parent.height, 0);
        compare(body.parent.parent.width, 0);
        compare(body.parent.parent.height, 0);

        menu.bodyW = 220;
        menu.bodyH = 180;
        menu.bodyRadius = 20;
        menu._up = true;
        compare(body.x, 90);
        compare(body.y, -110);
        compare(body.width, 220);
        compare(body.radius, 20);
        menu.enabled = false; // closing MouseArea must not disable the material
        verify(body.enabled);
        menu.nativeBodyMix = 0.2;
        compare(material.opacity, 0.2);
        menu.visible = false;
        verify(!body.visible);
    }
}
