import QtQml
import Applestia.Glass

// Loaded dynamically: missing client module must not prevent legacy shell startup.
QtObject {
    readonly property bool available: GlassManager.available
    readonly property bool active: GlassManager.active
}
