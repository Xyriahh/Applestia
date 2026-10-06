// Independent offscreen acceptance-API fixture. Never imported by production
// shell source or installed with Applestia.Glass; dependencies are test stubs.
import QtQuick
import Quickshell
import Quickshell.Io
import qs.services
import qs.components.glass

Scope {
    id: root
    readonly property bool testEnabled: Quickshell.env("APPLESTIA_V2_TEST_HOOKS") === "1"

    IpcHandler {
        id: ipc
        objectName: "applestiaV2TestIpc"
        target: root.testEnabled ? "v2Test" : ""

        function capability(): string {
            return JSON.stringify({
                active: InnerGlass.nativeCapability?.active ?? false,
                available: InnerGlass.nativeCapability?.available ?? false,
                shapesAvailable: InnerGlass.shapesAvailable,
                overlayFrameScreens: Object.keys(InnerGlass.frames)
            });
        }

        function stats(): string {
            return JSON.stringify({
                configPath: Quickshell.shellDir,
                active: InnerGlass.nativeCapability?.active ?? false,
                available: InnerGlass.nativeCapability?.available ?? false,
                shapesAvailable: InnerGlass.shapesAvailable
            });
        }

        function tab(index: int): void {
            if (!root.testEnabled) return;
            const state = ShellState.forActive();
            if (state) state.dashboardTab = Math.max(0, Math.min(4, index));
        }

        function show(drawer: string, value: bool): void {
            if (!root.testEnabled || !["dashboard", "utilities", "sidebar", "launcher", "osd"].includes(drawer)) return;
            const state = ShellState.forActive();
            if (state) state[drawer] = value;
        }

        // Presentation-only test state: no Recorder dependency or clicked calls.
        function recordMenu(value: bool): string {
            if (!root.testEnabled) return "disabled";
            const components = ShellState.componentsForActive();
            const button = components?.find("applestiaV2RecordSplit");
            if (!button) return "missing";
            button.expanded = value;
            return button.expanded ? "1" : "0";
        }
    }
}
