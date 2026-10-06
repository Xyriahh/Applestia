#pragma once

// Internal test access only; not installed or exposed through QML. Tests use a
// socket-pair Wayland proxy, never Qt's real display or a live compositor.
class QQuickWindow;
struct applestia_glass_shapes_v1;
namespace GlassClientTest {
void attachAssociation(QQuickWindow *window, applestia_glass_shapes_v1 *proxy);
applestia_glass_shapes_v1 *association(QQuickWindow *window);
bool publishFrame(QQuickWindow *window);
}
