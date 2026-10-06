// Isolated libwayland unit harness; never connects to a compositor. Including
// the implementation permits exercising dispatch handlers and inert objects
// without a client/server event loop or Hyprland-dependent test scaffolding.
#include <stdlib.h>
#include <wayland-server.h>
// Isolate failed-start paths without exhausting machine memory or touching a
// compositor. Only helper allocations/global creation use these wrappers.
static int fail_calloc, fail_global_create;
static void* test_calloc(size_t count, size_t size) {
    if (fail_calloc) {
        fail_calloc = 0;
        return NULL;
    }
    return calloc(count, size);
}
static struct wl_global* test_global_create(struct wl_display* display, const struct wl_interface* interface,
                                            int version, void* data, wl_global_bind_func_t bind) {
    if (fail_global_create) {
        fail_global_create = 0;
        return NULL;
    }
    return wl_global_create(display, interface, version, data, bind);
}
#define calloc test_calloc
#define wl_global_create test_global_create
#include "helper.c"
#undef calloc
#undef wl_global_create
#include <assert.h>
#include <stdio.h>
#include <sys/socket.h>
#include <unistd.h>

static unsigned created_count, destroyed_count;
static void created(struct wl_resource* surface, void* userdata) {
    assert(surface && userdata == &created_count);
    ++created_count;
}
static void destroyed(struct wl_resource* surface, void* userdata) {
    assert(surface && userdata == &created_count);
    ++destroyed_count;
}
static wl_fixed_t fixed(double x) { return wl_fixed_from_double(x); }
static void add(struct wl_client* client, struct wl_resource* resource) {
    handle_add(client, resource, fixed(10), fixed(20), fixed(100), fixed(40),
               fixed(80), fixed(40), fixed(20), fixed(60), UINT32_MAX, 0x12345678u, "applestia_clear");
}
static struct applestia_shapes_state state;
// Single-commit convenience for these request-handler tests. Actual queued
// identity/synchronized publication is covered by test-commits.cpp, using the
// same CShapeCommitSnapshots implementation as the compositor adapter.
static struct applestia_shapes_state single_snapshot;
static void api_snapshot(struct wl_resource* surface) {
    assert(api_get_pending(surface, &single_snapshot));
}
static void api_apply(struct wl_resource* surface) {
    api_apply_state(surface, &single_snapshot);
}

int main(void) {
    struct wl_display* display = wl_display_create();
    assert(display);
    int sockets[2];
    assert(socketpair(AF_UNIX, SOCK_STREAM, 0, sockets) == 0);
    struct wl_client* client = wl_client_create(display, sockets[0]);
    assert(client);
    api_set_callbacks(&created_count, created, destroyed);
    fail_calloc = 1;
    assert(api_start(display) == -1 && !g_global && !g_global_retirement);
    api_stop(); // plugin's failed-init cleanup must clear callbacks even without a global
    assert(!g_created && !g_destroyed && !g_userdata);
    api_set_callbacks(&created_count, created, destroyed);
    fail_global_create = 1;
    assert(api_start(display) == -1 && !g_global && !g_global_retirement);
    api_stop();
    assert(!g_created && !g_destroyed && !g_userdata);
    assert(api_start(display) == 0);
    assert(api_start(display) == 0); // idempotent
    api_set_callbacks(&created_count, created, destroyed);
    struct wl_resource* surface = wl_resource_create(client, &wl_surface_interface, 1, 2);
    assert(surface && !api_get(surface, &state));
    bind_manager(client, g_global, 1, 3);
    struct wl_resource* manager = wl_client_get_object(client, 3);
    assert(manager);
    handle_get_shapes(client, manager, 4, surface);
    struct wl_resource* resource = wl_client_get_object(client, 4);
    assert(resource && created_count == 1);
    assert(api_get(surface, &state) && state.count == 0); // engaged empty
    uint64_t empty_generation = api_generation(surface);
    assert(empty_generation > 0);
    handle_clip(client, resource, 0, 0, 0, 0);
    handle_opacity(client, resource, fixed(0.2)); // no last shape: no-op
    add(client, resource);
    assert(api_get(surface, &state) && state.count == 0);
    assert(api_generation(surface) == empty_generation); // pending isn't damage
    api_snapshot(surface); // wl_surface.commit request, synchronized state cached
    handle_begin(client, resource); // requests after the commit must not leak
    assert(api_get(surface, &state) && state.count == 0);
    api_apply(surface); // ancestor flush of the cached commit
    assert(api_get(surface, &state) && state.count == 1);
    const struct applestia_shape* s = &state.shapes[0];
    assert(s->x == 10 && s->y == 20 && s->width == 100 && s->height == 40);
    assert(s->radii[0] + s->radii[3] <= 40.0001f);
    assert(fabsf(s->radii[0] / s->radii[1] - 2.f) < 0.0001f); // common CSS scale
    assert(s->depth == 128 && s->tint == 0x12345678u);
    assert(!strcmp(s->preset, "applestia_clear") && s->opacity == 1 && !s->has_clip);
    uint64_t generation = api_generation(surface);
    assert(generation > empty_generation);
    api_apply(surface);
    assert(api_generation(surface) == generation); // identical applied state
    api_snapshot(surface);
    api_apply(surface);
    assert(api_get(surface, &state) && state.count == 0); // committed clear
    assert(api_generation(surface) > generation);

    handle_add(client, resource, fixed(-100000), fixed(100000), fixed(100000), fixed(60),
               fixed(-10), 0, 0, 0, 0, 0, "unsafe/name");
    handle_clip(client, resource, fixed(-100000), fixed(100000), fixed(-1), fixed(5));
    handle_opacity(client, resource, fixed(-2));
    api_snapshot(surface); api_apply(surface); api_get(surface, &state);
    assert(state.count == 1);
    s = &state.shapes[0];
    assert(s->x == -32768 && s->y == 32768 && s->width == 32768);
    assert(s->radii[0] == 0 && !s->preset[0] && s->opacity == 0);
    assert(s->has_clip && s->clip[0] == -32768 && s->clip[1] == 32768);
    assert(s->clip[2] == 0 && s->clip[3] == 0); // empty clip stays empty
    handle_opacity(client, resource, fixed(2));
    api_snapshot(surface); api_apply(surface); api_get(surface, &state);
    assert(state.shapes[0].opacity == 1);

    // Each accepted add opens an annotation scope; a rejected add closes it.
    handle_begin(client, resource);
    add(client, resource);
    handle_clip(client, resource, fixed(10), fixed(20), fixed(30), fixed(40));
    handle_opacity(client, resource, fixed(0.5));
    const struct applestia_shape accepted = find_shapes(surface)->pending.shapes[0];
    handle_add(client, resource, 0, 0, 0, fixed(20), 0, 0, 0, 0, 0, 0, "");
    handle_clip(client, resource, 0, 0, 0, 0);
    handle_opacity(client, resource, 0);
    assert(find_shapes(surface)->pending.count == 1);
    assert(!memcmp(&accepted, &find_shapes(surface)->pending.shapes[0], sizeof(accepted)));
    api_snapshot(surface); api_apply(surface); api_get(surface, &state);
    assert(state.count == 1 && !memcmp(&accepted, &state.shapes[0], sizeof(accepted)));
    add(client, resource); // acceptance reopens scope, targeting only the new shape
    handle_opacity(client, resource, fixed(0.25));
    handle_clip(client, resource, fixed(5), fixed(6), fixed(7), fixed(8));
    assert(!memcmp(&accepted, &find_shapes(surface)->pending.shapes[0], sizeof(accepted)));
    assert(find_shapes(surface)->pending.shapes[1].opacity == 0.25f);
    assert(find_shapes(surface)->pending.shapes[1].has_clip);
    shapes_impl.clear(client, resource);
    handle_clip(client, resource, 0, 0, fixed(1), fixed(1));
    handle_opacity(client, resource, fixed(0.125));
    assert(!find_shapes(surface)->pending.count && !find_shapes(surface)->annotation_target);
    add(client, resource);
    assert(find_shapes(surface)->pending.shapes[0].opacity == 1.f);
    assert(!find_shapes(surface)->pending.shapes[0].has_clip);

    handle_begin(client, resource);
    handle_clip(client, resource, 0, 0, fixed(1), fixed(1));
    handle_opacity(client, resource, fixed(0.125));
    assert(!find_shapes(surface)->pending.count && !find_shapes(surface)->annotation_target);
    handle_add(client, resource, 0, 0, fixed(-1), fixed(20), 0, 0, 0, 0, 0, 0, "");
    assert(find_shapes(surface)->pending.count == 0); // invalid extent is ignored
    for (unsigned i = 0; i < APPLESTIA_SHAPES_CAP; ++i)
        add(client, resource);
    handle_clip(client, resource, fixed(10), fixed(20), fixed(30), fixed(40));
    handle_opacity(client, resource, fixed(0.5));
    const struct applestia_shape last_accepted = find_shapes(surface)->pending.shapes[APPLESTIA_SHAPES_CAP - 1];
    for (unsigned i = 0; i < 10; ++i) {
        add(client, resource); // over-cap adds reject and invalidate annotation scope
        handle_clip(client, resource, 0, 0, 0, 0);
        handle_opacity(client, resource, 0);
    }
    api_snapshot(surface); api_apply(surface); api_get(surface, &state);
    assert(state.count == APPLESTIA_SHAPES_CAP);
    assert(!memcmp(&last_accepted, &state.shapes[APPLESTIA_SHAPES_CAP - 1], sizeof(last_accepted)));

    handle_get_shapes(client, manager, 5, surface);
    struct wl_resource* duplicate = wl_client_get_object(client, 5);
    assert(duplicate && created_count == 1);
    assert(((struct shapes_object*)wl_resource_get_user_data(duplicate))->inert);
    handle_destroy(client, duplicate); // duplicate does not disconnect
    generation = api_generation(surface);
    handle_destroy(client, resource); // object first: listener must be unlinked
    assert(destroyed_count == 1 && !api_get(surface, &state));
    assert(api_generation(surface) > generation); // absence is not generation zero
    handle_get_shapes(client, manager, 6, surface);
    resource = wl_client_get_object(client, 6);
    assert(created_count == 2 && api_generation(surface) > generation);
    wl_resource_destroy(surface); // surface first: object stays valid and inert
    assert(destroyed_count == 2);
    assert(((struct shapes_object*)wl_resource_get_user_data(resource))->inert);
    handle_destroy(client, resource);
    assert(destroyed_count == 2);

    surface = wl_resource_create(client, &wl_surface_interface, 1, 7);
    handle_get_shapes(client, manager, 8, surface);
    resource = wl_client_get_object(client, 8);
    add(client, resource);
    api_snapshot(surface); api_apply(surface);
    assert(created_count == 3 && api_get(surface, &state));
    struct wl_global* old_global = g_global;
    api_stop();
    assert(!g_global && !g_created && !g_destroyed && !g_userdata);
    assert(!api_get(surface, &state));
    handle_begin(client, resource); add(client, resource);
    handle_get_shapes(client, manager, 9, surface); // old manager new_id is valid but inert
    assert(((struct shapes_object*)wl_resource_get_user_data(wl_client_get_object(client, 9)))->inert);
    bind_manager(client, old_global, 1, 10); // bind raced global removal
    assert(((struct manager_object*)wl_resource_get_user_data(wl_client_get_object(client, 10)))->inert);
    api_stop(); // idempotent, no callbacks survive
    assert(api_start(display) == 0); // adopt persistent library, new generation
    api_set_callbacks(&created_count, created, destroyed);
    bind_manager(client, g_global, 1, 11);
    struct wl_resource* new_manager = wl_client_get_object(client, 11);
    handle_get_shapes(client, new_manager, 12, surface);
    struct wl_resource* new_resource = wl_client_get_object(client, 12);
    assert(created_count == 4 && api_get(surface, &state) && state.count == 0);
    add(client, resource); // old objects cannot mutate the new association
    api_snapshot(surface); api_apply(surface);
    assert(api_get(surface, &state) && state.count == 0);
    handle_destroy(client, new_manager); // manager destruction doesn't destroy shapes
    add(client, new_resource); api_snapshot(surface); api_apply(surface);
    assert(api_get(surface, &state) && state.count == 1);
    api_stop();
    // Ordinary config deactivation/re-enable must create a fresh global every
    // time while old objects remain permanently inert, not reactivated.
    for (unsigned i = 0; i < 8; ++i) {
        assert(api_start(display) == 0);
        api_set_callbacks(&created_count, created, destroyed);
        bind_manager(client, g_global, 1, 13 + i * 2);
        struct wl_resource* cycle_manager = wl_client_get_object(client, 13 + i * 2);
        handle_get_shapes(client, cycle_manager, 14 + i * 2, surface);
        struct wl_resource* cycle_resource = wl_client_get_object(client, 14 + i * 2);
        assert(api_get(surface, &state) && !state.count);
        add(client, new_resource); // older generation must still be inert
        api_snapshot(surface); api_apply(surface);
        assert(api_get(surface, &state) && !state.count);
        add(client, cycle_resource); api_snapshot(surface); api_apply(surface);
        assert(api_get(surface, &state) && state.count == 1);
        api_stop();
        assert(!api_get(surface, &state));
    }
    wl_resource_destroy(surface); // stopped-generation listeners are unlinked
    assert(destroyed_count == 2);
    wl_display_destroy_clients(display);
    close(sockets[1]);
    // Retired globals belong to this display; reclaim before its destruction.
    struct retired_global *retired, *tmp;
    wl_list_for_each_safe(retired, tmp, &g_retired, link) {
        wl_global_destroy(retired->global);
        wl_list_remove(&retired->link);
        free(retired);
    }
    wl_display_destroy(display);
    puts("shapes-helper: failed start, pending/cached/applied, sanitization, annotation scopes, cap, generation, destruction and reload tests passed");
    return 0;
}
