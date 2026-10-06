pragma ComponentBehavior: Bound

import QtQuick
import Quickshell
import Quickshell.Wayland
import qs.components
import qs.components.containers
import qs.components.glass
import qs.services

// Applestia: true stacked liquid glass for controls.
//
// A glass button/pill/card inside a glass panel lives in the SAME surface as the
// panel, so the compositor could only ever bend the desktop behind both. Here
// each registered drawer glass surface (InnerGlass.frames) is redrawn as a faint
// body in a click-through overlay layer above the drawers; hyprglass (silhouette
// mode, preset applestia_inner, no blur) refracts the FINISHED drawer image
// beneath it, so the parent glass, cards and content bend at the rim like Apple's
// controls. Labels stay in the drawer layer, sharp. Two levels so a button on a
// card gets its own rim instead of merging into the card's silhouette.
//
// Each body is BOUND to its live control through a TransformWatcher, so it moves
// in the same frame as the control: always there, no fade, no catching up.
//
// Reusable: ANY StyledRect/StyledClippingRect with `glass: true` in the drawer
// window gets this automatically. Its lens width adapts to its shape (a fifth of
// its shortest side, kept clear of coloured content; override with `lensWidth`,
// opt out with `lensEnabled: false`). The compositor sets the lens width per
// layer, so there is one overlay layer per (nesting level × lens size):
// applestia-lens-l{1,2}-{small,medium,large} (InnerGlass.rimSizes).
Variants {
    model: InnerGlass.shapesAvailable ? [] : Screens.screens

    Scope {
        id: scope

        required property ShellScreen modelData

        Variants {
            model: [0, 1, 2, 3, 4, 5] // level = 1 + i / 3, size = i % 3

            GlassLevel {
                required property int modelData

                screen: scope.modelData
                screenName: scope.modelData.name
                level: 1 + Math.floor(modelData / 3)
                size: modelData % 3
            }
        }
    }

    component GlassLevel: StyledWindow {
        id: win

        required property int level
        required property int size
        required property string screenName
        readonly property var entries: (InnerGlass.frames[screenName] ?? []).filter(e => e.size === size && (level === 1 ? e.depth === 0 : e.depth >= 1))

        name: `lens-l${level}-${InnerGlass.rimNames[size]}`
        anchors.top: true
        anchors.bottom: true
        anchors.left: true
        anchors.right: true
        exclusionMode: ExclusionMode.Ignore
        WlrLayershell.layer: WlrLayer.Overlay
        WlrLayershell.keyboardFocus: WlrKeyboardFocus.None
        mask: Region {} // click-through: the real controls are below
        visible: InnerGlass.available && !InnerGlass.shapesAvailable

        // Glass only over the union of the bodies (+ padding), never the whole
        // window: outside the silhouette the plugin would fall back to one box the
        // size of the screen, whose rim bends whatever sits at the screen edge
        // (the bar). Empty when every panel is closed. Explicit geometry: a Region
        // bound to a computed plain Item never registered.
        property rect union: Qt.rect(0, 0, 0, 0)

        function updateUnion(): void {
            let l = 1e9, t = 1e9, r = -1e9, b = -1e9;
            for (const e of entries) {
                const it = e.item;
                const root = it?.QsWindow.window?.contentItem;
                if (!it || !root)
                    continue;
                const box = it.mapToItem(root, Qt.rect(0, 0, it.width, it.height));
                l = Math.min(l, Math.max(box.x, e.clip[0]));
                t = Math.min(t, Math.max(box.y, e.clip[1]));
                r = Math.max(r, Math.min(box.x + box.width, e.clip[2]));
                b = Math.max(b, Math.min(box.y + box.height, e.clip[3]));
            }
            const pad = 24;
            union = r > l && b > t ? Qt.rect(Math.max(0, l - pad), Math.max(0, t - pad), r - l + pad * 2, b - t + pad * 2) : Qt.rect(0, 0, 0, 0);
        }

        onEntriesChanged: updateUnion()

        Timer {
            interval: 16
            running: win.entries.length > 0
            repeat: true
            onTriggered: win.updateUnion()
        }

        BackgroundEffect.blurRegion: Region {
            x: win.union.x
            y: win.union.y
            width: win.union.width
            height: win.union.height
        }

        Repeater {
            model: 64 // fixed pool: a slot keeps its delegate while its control stays

            Item {
                id: slot

                required property int index
                readonly property var entry: win.entries[index] ?? null
                readonly property Item target: entry?.item ?? null
                readonly property Item targetRoot: target?.QsWindow.window?.contentItem ?? null

                TransformWatcher {
                    id: watcher

                    a: slot.targetRoot
                    b: slot.target
                }

                // live geometry of the control in its window (= this window: both fill the screen)
                readonly property rect box: {
                    watcher.transform; // mapToItem is not reactive: this re-evaluates on any transform change
                    const t = target;
                    if (!t || !targetRoot)
                        return Qt.rect(0, 0, 0, 0);
                    return t.mapToItem(targetRoot, Qt.rect(0, 0, t.width, t.height));
                }

                visible: target !== null && (entry?.opacity ?? 0) > 0.01
                x: entry ? entry.clip[0] : 0
                y: entry ? entry.clip[1] : 0
                width: entry ? entry.clip[2] - entry.clip[0] : 0
                height: entry ? entry.clip[3] - entry.clip[1] : 0
                clip: true

                Rectangle {
                    readonly property real s: slot.target && slot.target.width > 0 ? Math.min(slot.box.width / slot.target.width, slot.box.height / slot.target.height) : 1
                    readonly property real maxR: Math.min(width, height) / 2

                    x: slot.box.x - slot.x
                    y: slot.box.y - slot.y
                    width: slot.box.width
                    height: slot.box.height
                    topLeftRadius: Math.min(maxR, (slot.target?.topLeftRadius ?? slot.target?.radius ?? 0) * s)
                    topRightRadius: Math.min(maxR, (slot.target?.topRightRadius ?? slot.target?.radius ?? 0) * s)
                    bottomRightRadius: Math.min(maxR, (slot.target?.bottomRightRadius ?? slot.target?.radius ?? 0) * s)
                    bottomLeftRadius: Math.min(maxR, (slot.target?.bottomLeftRadius ?? slot.target?.radius ?? 0) * s)
                    // the shape the compositor turns into a lens (≥ silhouette threshold 0.04)
                    color: Qt.rgba(1, 1, 1, 0.05)
                    opacity: slot.entry?.opacity ?? 0
                }
            }
        }
    }
}
