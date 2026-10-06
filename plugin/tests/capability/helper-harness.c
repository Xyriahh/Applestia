// Private libwayland globals exercising the same start/stop used by
// GlassShapes::init/exit. No server socket name, compositor, or GPU connection.
#include "../../src/shapes-helper/helper.c"
#include <assert.h>
#include <sys/socket.h>
#include <unistd.h>

static struct wl_display* display;
static struct wl_client* client;
static int peer;
static struct wl_resource *surface, *old_manager, *old_shapes;

void fixture_start(void) {
    display = wl_display_create();
    assert(display);
    int sockets[2];
    assert(socketpair(AF_UNIX, SOCK_STREAM, 0, sockets) == 0);
    client = wl_client_create(display, sockets[0]);
    assert(client);
    peer = sockets[1];
    surface = wl_resource_create(client, &wl_surface_interface, 1, 2);
    assert(surface);
}
int fixture_init(void) { return api_start(display) == 0; }
void fixture_exit(void) { api_stop(); }
int fixture_active(void) { return g_global != NULL; }
unsigned fixture_global_name(void) { assert(g_global); return wl_global_get_name(g_global, client); }
void fixture_bind_old(void) {
    bind_manager(client, g_global, 1, 3);
    old_manager = wl_client_get_object(client, 3);
    handle_get_shapes(client, old_manager, 4, surface);
    old_shapes = wl_client_get_object(client, 4);
    struct applestia_shapes_state state;
    assert(old_shapes && api_get(surface, &state));
}
void fixture_assert_old_inert(void) {
    struct manager_object* manager = wl_resource_get_user_data(old_manager);
    struct shapes_object* shapes = wl_resource_get_user_data(old_shapes);
    assert(manager->inert && shapes->inert && !shapes->surface && !shapes->listening);
    struct applestia_shapes_state state;
    assert(!api_get(surface, &state));
}
void fixture_bind_new(void) {
    bind_manager(client, g_global, 1, 5);
    handle_get_shapes(client, wl_client_get_object(client, 5), 6, surface);
    struct applestia_shapes_state state;
    assert(api_get(surface, &state)); // Newly bound shape association really active.
}
void fixture_finish(void) {
    api_stop();
    wl_client_destroy(client);
    close(peer);
    wl_display_destroy(display);
}
