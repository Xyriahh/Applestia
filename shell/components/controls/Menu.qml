pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Effects
import QtQuick.Layouts
import Quickshell
import Caelestia.Blobs
import Caelestia.Config
import qs.components
import qs.components.effects
import qs.components.glass
import qs.services
import qs.modules.drawers

MouseArea {
    id: root

    enum Side {
        Top,
        Bottom,
        Left,
        Right
    }

    required property Item attachTo
    property int attachSideX: Menu.Right
    property int attachSideY: Menu.Bottom
    property int thisSideX: Menu.Right
    property int thisSideY: Menu.Top
    property real marginX
    property real marginY

    property list<MenuItem> items
    property MenuItem active: items[0] ?? null
    property bool expanded

    signal itemSelected(item: MenuItem)

    parent: {
        const win = QsWindow.window;
        const contentWin = win as ContentWindow; // If inside the drawer content window, put it inside the interaction wrapper so hover works
        return contentWin ? contentWin.interactionWrapper : (win as QsWindow).contentItem;
    }
    anchors.fill: parent

    enabled: expanded
    hoverEnabled: expanded
    cursorShape: expanded ? Qt.ArrowCursor : undefined
    onClicked: expanded = false

    // Applestia: Apple-style liquid menu (modelled on an iOS 26 screen
    // recording). Two metaballs (Caelestia.Blobs): a pill in the button's shape
    // squishes and dissolves into a narrow droplet that falls from the button,
    // and that droplet swells into the menu, centred on the button, with a small
    // spring. The items are there the whole time, blurred, and sharpen as the
    // shape settles. Closing runs the same path backwards: the menu drains up
    // through a liquid neck into the re-forming pill.
    // progress: 0 = the button, 1 = the open menu.
    property real progress: expanded ? 1 : 0
    readonly property real morph: progress // used by SplitButton

    visible: progress > 0.001

    Behavior on progress {
        NumberAnimation {
            duration: root.expanded ? 520 : 360
            easing.type: root.expanded ? Easing.OutSine : Easing.InOutSine
        }
    }

    function clamp01(v: real): real {
        return Math.max(0, Math.min(1, v));
    }
    function seg(p: real, a: real, b: real): real {
        return clamp01((p - a) / (b - a));
    }
    function easeInOut(t: real): real {
        return t < 0.5 ? 4 * t * t * t : 1 - Math.pow(-2 * t + 2, 3) / 2;
    }
    function easeOutBack(t: real): real {
        const c1 = 1.2;
        const c3 = c1 + 1;
        return 1 + c3 * Math.pow(t - 1, 3) + c1 * Math.pow(t - 1, 2);
    }

    TransformWatcher {
        id: watcher

        a: root.parent
        b: root.attachTo
    }

    // Where the menu ends up (same placement rules as Caelestia's Menu)
    readonly property real targetWidth: Math.max(200, column.implicitWidth + column.anchors.margins * 2)
    readonly property real targetHeight: column.implicitHeight + column.anchors.margins * 2
    readonly property point targetPos: {
        watcher.transform; // mapToItem is not reactive so this forces updates
        const item = root.attachTo;
        const p = item.mapToItem(root.parent, 0, 0);
        // centred on the button, covering it (Apple's button menus)
        let x = p.x + item.width / 2 - targetWidth / 2 + root.marginX;
        x = Math.max(8, Math.min((root.parent?.width ?? x + targetWidth + 8) - targetWidth - 8, x));
        const y = root.thisSideY === Menu.Bottom ? p.y + item.height - targetHeight + root.marginY : p.y + root.marginY;
        return Qt.point(x, y);
    }
    // Where it starts from: the button itself
    readonly property rect sourceRect: {
        watcher.transform;
        const item = root.attachTo;
        const p = item.mapToItem(root.parent, 0, 0);
        return Qt.rect(p.x, p.y, item.width, item.height);
    }
    readonly property real sourceRadius: (attachTo as StyledRect)?.radius ?? (attachTo as Rectangle)?.radius ?? attachTo.height / 2

    function lerp(a: real, b: real, t: real): real {
        return a + (b - a) * t;
    }

    // ── geometry along the path (all in root.parent coordinates) ─────────────
    readonly property real _s1: seg(progress, 0.0, 0.32)                // droplet forms
    readonly property real _s2: seg(progress, 0.26, 1.0)                // droplet swells into the menu
    readonly property real _grow: _s2 > 0 ? easeOutBack(_s2) : 0
    readonly property real _move: easeInOut(_s2)
    readonly property bool _up: thisSideY === Menu.Bottom

    readonly property real _btnCx: sourceRect.x + sourceRect.width / 2
    readonly property real _dropW: Math.max(sourceRect.width * 0.55, 18)
    readonly property real _dropH: Math.max(sourceRect.height * 1.7, targetHeight * 0.4)

    readonly property real bodyW: _s2 > 0 ? lerp(_dropW, targetWidth, _grow) : lerp(sourceRect.width * 0.8, _dropW, easeInOut(_s1))
    readonly property real bodyH: _s2 > 0 ? lerp(_dropH, targetHeight, _grow) : lerp(sourceRect.height, _dropH, easeInOut(_s1))
    readonly property real bodyCx: lerp(_btnCx, targetPos.x + targetWidth / 2, _move)
    readonly property real bodyEdge: _up ? lerp(sourceRect.y + sourceRect.height, targetPos.y + targetHeight, _move) : lerp(sourceRect.y, targetPos.y, _move)
    readonly property real bodyRadius: lerp(Math.min(bodyW, bodyH) / 2, Tokens.rounding.large, _move)

    // the button's pill: squishes, then dissolves into the droplet
    readonly property real _pillGone: easeInOut(seg(progress, 0.16, 0.42))
    readonly property real _squish: easeInOut(seg(progress, 0.0, 0.22))
    readonly property real pillW: sourceRect.width * lerp(1, 0.6, _squish) * (1 - _pillGone)
    readonly property real pillH: sourceRect.height * lerp(1, 0.78, _squish) * (1 - _pillGone)

    // Native rounded rectangles cannot represent the joined metaball neck.
    // Preserve the original pill/drop union until the pill is fully gone (.42),
    // then hand off the SAME evolving body geometry, never a final-size popup.
    readonly property bool nativeMaterial: InnerGlass.nativeEligible(root)
    readonly property real nativeBodyMix: nativeMaterial ? easeInOut(seg(progress, 0.42, 0.64)) : 0
    readonly property real bodyFade: easeInOut(seg(progress, 0.0, 0.12))
    readonly property color bodyTint: InnerGlass.innerGlassTint

    Loader {
        active: root.nativeMaterial
        // The MouseArea disables input on closing. Its visual material must not
        // inherit enabled:false and vanish before the reverse morph completes.
        parent: root.parent
        Component.onCompleted: {
            if (active && source.toString() === "") setSource("../glass/NativeMenuMaterial.qml", { menuOwner: root });
        }
        onActiveChanged: {
            if (active) setSource("../glass/NativeMenuMaterial.qml", { menuOwner: root });
            else source = "";
        }
    }

    Item {
        id: menu

        anchors.fill: parent

        MouseArea {
            x: root.bodyCx - root.bodyW / 2
            y: root._up ? root.bodyEdge - root.bodyH : root.bodyEdge
            width: root.bodyW
            height: root.bodyH
            hoverEnabled: true
            onWheel: e => e.accepted = true
        }

        // Liquid body: metaball union of the pill and the droplet/menu. The blob
        // renderer ignores colour alpha, so the glass tint alpha goes on this
        // flattened container. Outside a glass panel it becomes compositor glass.
        Item {
            anchors.fill: parent
            // the button's own inner-glass tint: the liquid is the button flowing out
            opacity: root.lerp(0, InnerGlass.innerGlassTint.a, root.easeInOut(root.seg(root.progress, 0.0, 0.12))) * (1 - root.nativeBodyMix)
            layer.enabled: true

            BlobGroup {
                id: liquid

                // translucent grey glass like Apple's menus, not an ink blob
                color: Qt.alpha(InnerGlass.innerGlassTint, 1)
                smoothing: 14
            }

            BlobRect {
                group: liquid
                visible: root.pillW > 0.5 && root.pillH > 0.5
                x: root._btnCx - root.pillW / 2
                y: root.sourceRect.y + (root.sourceRect.height - root.pillH) / 2
                implicitWidth: root.pillW
                implicitHeight: root.pillH
                radius: Math.min(root.pillW, root.pillH) / 2
            }

            BlobRect {
                group: liquid
                x: root.bodyCx - root.bodyW / 2
                y: root._up ? root.bodyEdge - root.bodyH : root.bodyEdge
                implicitWidth: root.bodyW
                implicitHeight: root.bodyH
                radius: root.bodyRadius
                deformScale: 0.00002 // a little jelly as it moves
            }
        }

        // specular rim once the shape has settled into the menu
        GlassFill {
            visible: !root.nativeMaterial
            x: root.targetPos.x
            y: root.targetPos.y
            width: root.targetWidth
            height: root.targetHeight
            radius: Tokens.rounding.large
            tint: "transparent"
            opacity: root.seg(root.progress, 0.82, 1.0)
        }

        // Items: present from early on, blurred, sharpening as the shape settles;
        // clipped to the body's bounds so they never spill outside the liquid
        Item {
            x: root.bodyCx - root.bodyW / 2
            y: root._up ? root.bodyEdge - root.bodyH : root.bodyEdge
            width: root.bodyW
            height: root.bodyH
            clip: true

            Item {
                id: contentHolder

                x: root.targetPos.x - parent.x
                y: root.targetPos.y - parent.y
                width: root.targetWidth
                height: root.targetHeight
                opacity: root.easeInOut(root.seg(root.progress, 0.3, 0.75))

                layer.enabled: root.progress < 0.999
                layer.effect: MultiEffect {
                    blurEnabled: true
                    blurMax: 40
                    blur: 1 - root.easeInOut(root.seg(root.progress, 0.35, 0.95))
                }

                ColumnLayout {
                    id: column

                    anchors.fill: parent
                    anchors.margins: Tokens.padding.extraSmall
                    spacing: 0

                    Repeater {
                        id: repeater

                        model: root.items

                        StyledRect {
                            id: item

                            required property int index
                            required property MenuItem modelData
                            readonly property bool active: modelData === root?.active

                            Layout.fillWidth: true
                            implicitWidth: menuOptionRow.implicitWidth + Tokens.padding.medium * 2
                            implicitHeight: menuOptionRow.implicitHeight + Tokens.padding.medium * 2

                            radius: Tokens.rounding.medium
                            color: Qt.alpha(Colours.palette.m3primary, active ? 0.85 : 0)

                            StateLayer {
                                radius: parent.radius
                                color: item.active ? Colours.palette.m3onPrimary : Colours.palette.m3onSurface
                                disabled: !root.expanded
                                onClicked: {
                                    root.itemSelected(item.modelData);
                                    root.active = item.modelData;
                                    item.modelData.clicked();
                                    root.expanded = false;
                                }
                            }

                            RowLayout {
                                id: menuOptionRow

                                anchors.fill: parent
                                anchors.margins: Tokens.padding.medium
                                spacing: Tokens.spacing.small

                                MaterialIcon {
                                    Layout.alignment: Qt.AlignVCenter
                                    text: item.modelData?.icon ?? ""
                                    color: item.active ? Colours.palette.m3onPrimary : Colours.palette.m3onSurfaceVariant
                                }

                                StyledText {
                                    Layout.alignment: Qt.AlignVCenter
                                    Layout.fillWidth: true
                                    text: item.modelData?.text ?? ""
                                    color: item.active ? Colours.palette.m3onPrimary : Colours.palette.m3onSurface
                                }

                                Loader {
                                    asynchronous: true
                                    Layout.alignment: Qt.AlignVCenter
                                    active: item.modelData?.trailingIcon.length > 0
                                    visible: active

                                    sourceComponent: MaterialIcon {
                                        text: item.modelData.trailingIcon
                                        color: item.active ? Colours.palette.m3onPrimary : Colours.palette.m3onSurfaceVariant
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
