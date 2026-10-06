pragma Singleton

import QtQuick
import Quickshell
import Quickshell.Io
import qs.services

// Registry of glass surfaces (StyledRect/StyledClippingRect `refractive`).
// Two consumers:
//  * drawer screens -> `frames` for modules/glasslayer/ControlGlass.qml, which
//    draws each glass body in an overlay layer above the drawers so the
//    compositor refracts the FINISHED panel beneath it (true stacked glass);
//  * Settings windows (`glassKey`) -> the runtime regions file read by the
//    hyprglass inner-rim shader (no overlay for those windows yet).
Singleton {
    id: root

    // screen name -> [{ item, clip: [l, t, r, b], opacity, depth }]
    // Geometry is NOT snapshotted here: ControlGlass binds each body to its live
    // item (TransformWatcher), so the glass moves in the same frame as the control.
    // Only membership, clip, opacity and depth come from this (polled) list.
    property var frames: ({})

    // lens widths (logical px) with one overlay layer each (applestia-glass.lua
    // registers applestia-lens-l{1,2}-{small,medium,large}); see ControlGlass.qml
    readonly property list<int> rimSizes: [5, 8, 12]
    readonly property list<string> rimNames: ["small", "medium", "large"]

    // Inner glass (cards, pills, glass buttons): a step frostier than the panel.
    // Shared by GlassFill and the liquid menu so a menu flows out in its button's colour.
    readonly property color innerGlassTint: Colours.light ? Qt.rgba(1, 1, 1, 0.24) : Qt.rgba(0.06, 0.06, 0.08, 0.26)
    property string lastOverlay

    property var surfaces: []
    property string lastPayload
    property double lastWrite: 0
    property bool available: false
    property bool enabled: true

    property var nativeCapability: null
    readonly property bool shapesAvailable: nativeCapability?.available ?? false
    onShapesAvailableChanged: collect()
    Component.onCompleted: {
        const component = Qt.createComponent("NativeCapability.qml");
        if (component.status === Component.Ready)
            nativeCapability = component.createObject(root);
    }

    // v2 currently renders shapes on drawer layers only. Floating Settings windows
    // keep their regions-file renderer, even when the shapes global is advertised.
    function nativeEligible(item) {
        const win = item?.QsWindow.window;
        return shapesAvailable && win?.name === "drawers" && !win.glassKey;
    }

    function registerSurface(item) {
        surfaces = [...surfaces, item];
    }

    function unregisterSurface(item) {
        surfaces = surfaces.filter(candidate => candidate !== item);
    }

    // number of glass surfaces enclosing `item` (0 = sits directly on the panel)
    function glassDepth(item) {
        let depth = 0;
        for (let a = item.parent; a; a = a.parent)
            if (a.glass === true && surfaceSet.has(a))
                depth++;
        return depth;
    }
    property var surfaceSet: new Set()
    onSurfacesChanged: surfaceSet = new Set(surfaces)

    function collect() {
        const screens = {};
        const overlay = {};
        const tinted = []; // [{ item, box }] coloured surfaces in drawer windows
        for (const item of (enabled ? surfaces : [])) {
            if (!item || !item.refractive)
                continue;
            const win = item.QsWindow.window;
            if (!win || (win.name !== "drawers" && !win.glassKey))
                continue;
            if (nativeEligible(item))
                continue;
            if (item.width < 2 || item.height < 2)
                continue;
            let opacity = 1;
            let left = 0, top = 0, right = win.width, bottom = win.height;
            for (let ancestor = item; ancestor; ancestor = ancestor.parent) {
                if (!ancestor.visible) { opacity = 0; break; }
                opacity *= ancestor.opacity;
                // ClippingRectangle clips visually without setting `clip` (dashboard tab pages!)
                if (ancestor.clip || ancestor.contentInsideBorder !== undefined) {
                    const box = ancestor.mapToItem(win.contentItem, Qt.rect(0, 0, ancestor.width, ancestor.height));
                    left = Math.max(left, box.x);
                    top = Math.max(top, box.y);
                    right = Math.min(right, box.x + box.width);
                    bottom = Math.min(bottom, box.y + box.height);
                }
            }
            if (opacity < 0.04 || left >= right || top >= bottom)
                continue;
            const box = item.mapToItem(win.contentItem, Qt.rect(0, 0, item.width, item.height));
            if (box.x + box.width <= left || box.x >= right || box.y + box.height <= top || box.y >= bottom)
                continue;
            const scale = Math.min(box.width / item.width, box.height / item.height);
            const radii = [item.topLeftRadius ?? item.radius, item.topRightRadius ?? item.radius,
                item.bottomRightRadius ?? item.radius, item.bottomLeftRadius ?? item.radius];
            const clampedRadii = radii.map(radius => Math.min(box.width / 2, box.height / 2, Math.max(0, radius * scale)));
            if (!win.glassKey) {
                // drawer glass bodies go to the overlay layers, not the regions file.
                // Tinted surfaces (selection pills, filled states, active workspace)
                // are drawn beneath the overlay, so no lens may bend them: tinted
                // glass gets no lens itself, and is remembered so a parent whose
                // rim band it sits in (a segmented track, the workspace pill) is
                // skipped too (see below).
                if (item.color && item.color.a >= 0.12) {
                    tinted.push({ item, box });
                    continue;
                }
                if (item.glass !== true || item.lensEnabled === false)
                    continue;
                const name = win.screen.name;
                if (!overlay[name]) overlay[name] = [];
                overlay[name].push({ item, clip: [Math.round(left), Math.round(top), Math.round(right), Math.round(bottom)],
                    opacity: Math.round(opacity * 50) / 50, depth: glassDepth(item) });
                continue;
            }
            const key = `window:${win.glassKey}`;
            if (!screens[key]) screens[key] = [];
            screens[key].push({ box: [box.x, box.y, box.width, box.height],
                radii: clampedRadii,
                clip: [left, top, right, bottom], opacity });
        }
        // Drop a body when a tinted descendant lies inside its rim band (it would
        // be smeared). Cards keep their lens: their active toggles sit deeper
        // inside the padding than the band.
        // Per-element lens width ("rim"): a fifth of the shortest side, or the
        // element's own `lensWidth`; then narrowed so no tinted surface (selection
        // pill, coloured badge/toggle) lies inside the band (it would be smeared).
        // Snapped down to one of `rimSizes` (one overlay layer per size); dropped
        // when not even the smallest fits (e.g. a track hugging its pill).
        for (const name of Object.keys(overlay)) {
            overlay[name] = overlay[name].filter(entry => {
                const it = entry.item;
                const b = it.mapToItem(it.QsWindow.window.contentItem, Qt.rect(0, 0, it.width, it.height));
                let rim = it.lensWidth > 0 ? it.lensWidth : Math.min(b.width, b.height) * 0.2;
                for (const t of tinted) {
                    // geometric, not hierarchical: indicators are often siblings drawn over the pill
                    const overlaps = t.box.x < b.x + b.width && t.box.x + t.box.width > b.x
                        && t.box.y < b.y + b.height && t.box.y + t.box.height > b.y;
                    if (!overlaps || t.item === it)
                        continue;
                    const inset = Math.min(t.box.x - b.x, (b.x + b.width) - (t.box.x + t.box.width),
                        t.box.y - b.y, (b.y + b.height) - (t.box.y + t.box.height));
                    rim = Math.min(rim, inset - 2);
                }
                let size = -1;
                for (let i = rimSizes.length - 1; i >= 0; i--)
                    if (rimSizes[i] <= rim) { size = i; break; }
                entry.size = size;
                return size >= 0;
            });
        }

        // change key: item identity (registry index) + clip/opacity/depth, not geometry
        const overlayJson = JSON.stringify(overlay, (k, v) => k === "item" ? surfaces.indexOf(v) : v); // includes size
        if (overlayJson !== lastOverlay) {
            lastOverlay = overlayJson;
            frames = overlay;
        }
        const payload = JSON.stringify(screens);
        const now = Date.now();
        if (payload !== lastPayload || now - lastWrite >= 1000) {
            lastPayload = payload;
            lastWrite = now;
            const rows = [];
            for (const key of Object.keys(screens)) {
                for (const entry of screens[key].slice(0, 128)) {
                    rows.push([JSON.stringify(key), ...entry.box, ...entry.radii, ...entry.clip, entry.opacity].join(" "));
                }
            }
            bridge.setText(`1 ${now} ${rows.length}\n${rows.join("\n")}\n`);
        }
    }

    FileView {
        id: bridge
        path: `${Quickshell.env("XDG_RUNTIME_DIR")}/applestia-inner-${Quickshell.env("HYPRLAND_INSTANCE_SIGNATURE")}.regions`
        atomicWrites: true
        printErrors: false
    }

    FileView {
        path: `${Quickshell.env("XDG_RUNTIME_DIR")}/applestia-inner-${Quickshell.env("HYPRLAND_INSTANCE_SIGNATURE")}.ready`
        watchChanges: true
        printErrors: false
        onFileChanged: reload()
        onLoaded: root.available = text().trim() === "1"
        onLoadFailed: root.available = false
    }

    Timer {
        interval: 16 // overlay bodies follow their controls at 60 Hz
        running: root.surfaces.some(item => item && (!root.shapesAvailable || item.QsWindow.window?.glassKey))
        repeat: true
        onTriggered: root.collect()
    }
}
