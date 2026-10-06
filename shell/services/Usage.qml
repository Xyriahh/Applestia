pragma Singleton

import QtQuick
import Quickshell
import Quickshell.Io

Singleton {
    id: root

    property int refCount: 0
    property var data: null
    property string error: ""
    readonly property bool loading: proc.running

    readonly property string script: `${Quickshell.env("HOME")}/.local/share/caelestia-usage/usage.py`

    function reload(): void {
        if (proc.running)
            return;
        proc.running = true;
    }

    Process {
        id: proc

        command: ["python3", root.script]

        stdout: StdioCollector {
            onStreamFinished: {
                try {
                    const parsed = JSON.parse(text);
                    root.error = parsed.error ?? "";
                    if (!parsed.error)
                        root.data = parsed;
                } catch (error) {
                    root.error = `Unable to parse usage data: ${error}`;
                }
            }
        }

        stderr: StdioCollector {
            onStreamFinished: {
                if (text.trim())
                    console.warn(`caelestia usage: ${text.trim()}`);
            }
        }
    }

    Timer {
        interval: 10 * 60 * 1000 // 10 minutes
        running: root.refCount > 0
        repeat: true
        triggeredOnStart: true

        onTriggered: root.reload()
    }
}
