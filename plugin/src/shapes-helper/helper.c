// Persistent protocol resources/interfaces/handlers must never live in the
// unloadable plugin. This library depends only on libwayland-server and is
// loaded RTLD_NODELETE. stop() clears every callable pointer into the plugin.
#include "applestia-glass-shapes-v1-server-protocol.h"
#include "applestia_shapes_helper_api.h"

#include <wayland-server.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

struct manager_object {
    struct wl_resource* resource;
    int inert;
    struct wl_list link;
};
struct shapes_object {
    struct wl_resource* surface;
    struct wl_listener surface_destroy_listener;
    int listening, inert;
    int annotation_target; // most recent add accepted; reset/rejection invalidates it
    struct applestia_shapes_state pending, current;
    uint64_t generation;
    struct wl_list link;
};
struct retired_global {
    struct wl_global* global;
    struct wl_list link;
};
static struct wl_list g_managers, g_shapes, g_retired;
static int g_initialized;
static struct wl_global* g_global;
static struct retired_global* g_global_retirement;
static uint64_t g_generation;
static void* g_userdata;
static applestia_shapes_surface_fn g_created, g_destroyed;

static void ensure_lists(void) {
    if (g_initialized)
        return;
    wl_list_init(&g_managers);
    wl_list_init(&g_shapes);
    wl_list_init(&g_retired);
    g_initialized = 1;
}
static struct shapes_object* find_shapes(struct wl_resource* surface) {
    struct shapes_object* shapes;
    wl_list_for_each(shapes, &g_shapes, link)
        if (!shapes->inert && shapes->surface == surface)
            return shapes;
    return NULL;
}
static float bounded(double value, float low, float high) {
    if (!isfinite(value))
        return low;
    return value < low ? low : value > high ? high : (float)value;
}
static float coord(wl_fixed_t value) { return bounded(wl_fixed_to_double(value), -32768.f, 32768.f); }
static float extent(wl_fixed_t value) { return bounded(wl_fixed_to_double(value), 0.f, 32768.f); }

static void handle_destroy(struct wl_client* client, struct wl_resource* resource) {
    (void)client;
    wl_resource_destroy(resource);
}
static void handle_begin(struct wl_client* client, struct wl_resource* resource) {
    (void)client;
    struct shapes_object* shapes = wl_resource_get_user_data(resource);
    if (!shapes->inert) {
        memset(&shapes->pending, 0, sizeof(shapes->pending));
        shapes->annotation_target = 0;
    }
}
static void handle_add(struct wl_client* client, struct wl_resource* resource,
                       wl_fixed_t x, wl_fixed_t y, wl_fixed_t width, wl_fixed_t height,
                       wl_fixed_t tl, wl_fixed_t tr, wl_fixed_t br, wl_fixed_t bl,
                       uint32_t depth, uint32_t tint, const char* preset) {
    (void)client;
    struct shapes_object* shapes = wl_resource_get_user_data(resource);
    if (shapes->inert)
        return;
    // An ignored add still ends the previous shape's annotation scope. Without
    // this, annotations intended for an invalid/over-cap shape alter its sibling.
    shapes->annotation_target = 0;
    if (shapes->pending.count >= APPLESTIA_SHAPES_CAP)
        return;
    const float w = extent(width), h = extent(height);
    if (w <= 0.f || h <= 0.f)
        return;
    struct applestia_shape* s = &shapes->pending.shapes[shapes->pending.count++];
    memset(s, 0, sizeof(*s));
    s->x = coord(x); s->y = coord(y); s->width = w; s->height = h;
    s->radii[0] = extent(tl); s->radii[1] = extent(tr);
    s->radii[2] = extent(br); s->radii[3] = extent(bl);
    // CSS uses ONE common scale for all four corners, not independent clamps.
    const float sums[4] = {s->radii[0] + s->radii[1], s->radii[3] + s->radii[2],
                           s->radii[0] + s->radii[3], s->radii[1] + s->radii[2]};
    float scale = 1.f;
    for (int i = 0; i < 4; ++i) {
        const float edge = i < 2 ? w : h;
        if (sums[i] > edge && edge / sums[i] < scale)
            scale = edge / sums[i];
    }
    for (int i = 0; i < 4; ++i)
        s->radii[i] *= scale;
    s->depth = depth > 128u ? 128u : depth;
    s->tint = tint;
    s->opacity = 1.f;
    // Config preset identifiers: reject control/path characters to default,
    // truncate overlong otherwise-safe names, always NUL-terminate.
    if (preset) {
        size_t n = 0;
        int valid = 1;
        for (; preset[n] && n < APPLESTIA_SHAPES_PRESET_CAP - 1u; ++n) {
            const unsigned char c = (unsigned char)preset[n];
            if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                  (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '.')) {
                valid = 0;
                break;
            }
        }
        if (valid)
            memcpy(s->preset, preset, n);
    }
    shapes->annotation_target = 1;
}
static void handle_clip(struct wl_client* client, struct wl_resource* resource,
                        wl_fixed_t x, wl_fixed_t y, wl_fixed_t width, wl_fixed_t height) {
    (void)client;
    struct shapes_object* shapes = wl_resource_get_user_data(resource);
    if (shapes->inert || !shapes->annotation_target || !shapes->pending.count)
        return;
    struct applestia_shape* s = &shapes->pending.shapes[shapes->pending.count - 1u];
    // Keep the lens box/radii intact: ancestor clipping is flat, not a new edge.
    s->has_clip = 1;
    s->clip[0] = coord(x); s->clip[1] = coord(y);
    s->clip[2] = extent(width); s->clip[3] = extent(height);
    if (s->clip[2] <= 0.f || s->clip[3] <= 0.f)
        s->clip[2] = s->clip[3] = 0.f;
}
static void handle_opacity(struct wl_client* client, struct wl_resource* resource, wl_fixed_t opacity) {
    (void)client;
    struct shapes_object* shapes = wl_resource_get_user_data(resource);
    if (!shapes->inert && shapes->annotation_target && shapes->pending.count)
        shapes->pending.shapes[shapes->pending.count - 1u].opacity = bounded(wl_fixed_to_double(opacity), 0.f, 1.f);
}
static const struct applestia_glass_shapes_v1_interface shapes_impl = {
    .destroy = handle_destroy, .begin = handle_begin, .add_shape = handle_add,
    .clear = handle_begin, .set_clip = handle_clip, .set_opacity = handle_opacity,
};
static void surface_destroy(struct wl_listener* listener, void* data) {
    (void)data;
    struct shapes_object* shapes = wl_container_of(listener, shapes, surface_destroy_listener);
    struct wl_resource* surface = shapes->surface;
    wl_list_remove(&shapes->surface_destroy_listener.link);
    shapes->listening = 0;
    shapes->surface = NULL;
    shapes->inert = 1;
    ++g_generation;
    if (g_destroyed)
        g_destroyed(surface, g_userdata); // raw resource only; Hyprland object may already be freed
}
static void shapes_destroy(struct wl_resource* resource) {
    struct shapes_object* shapes = wl_resource_get_user_data(resource);
    if (shapes->listening)
        wl_list_remove(&shapes->surface_destroy_listener.link);
    if (!shapes->inert && shapes->surface) {
        ++g_generation;
        if (g_destroyed)
            g_destroyed(shapes->surface, g_userdata);
    }
    wl_list_remove(&shapes->link);
    free(shapes);
}
static void handle_get_shapes(struct wl_client* client, struct wl_resource* manager_resource,
                              uint32_t id, struct wl_resource* surface) {
    struct manager_object* manager = wl_resource_get_user_data(manager_resource);
    struct wl_resource* resource = wl_resource_create(client, &applestia_glass_shapes_v1_interface, 1, id);
    if (!resource) {
        wl_client_post_no_memory(client);
        return;
    }
    struct shapes_object* shapes = calloc(1, sizeof(*shapes));
    if (!shapes) {
        wl_resource_destroy(resource);
        wl_client_post_no_memory(client);
        return;
    }
    // Even a raced old-manager request must allocate the new_id, but inert.
    shapes->inert = manager->inert || find_shapes(surface) != NULL;
    shapes->surface = shapes->inert ? NULL : surface;
    shapes->generation = ++g_generation;
    wl_list_insert(&g_shapes, &shapes->link);
    wl_resource_set_implementation(resource, &shapes_impl, shapes, shapes_destroy);
    if (!shapes->inert) {
        shapes->surface_destroy_listener.notify = surface_destroy;
        wl_resource_add_destroy_listener(surface, &shapes->surface_destroy_listener);
        shapes->listening = 1;
        if (g_created)
            g_created(surface, g_userdata);
    }
}
static const struct applestia_glass_shapes_manager_v1_interface manager_impl = {
    .destroy = handle_destroy, .get_shapes = handle_get_shapes,
};
static void manager_destroy(struct wl_resource* resource) {
    struct manager_object* manager = wl_resource_get_user_data(resource);
    wl_list_remove(&manager->link);
    free(manager);
}
static void bind_manager(struct wl_client* client, void* data, uint32_t version, uint32_t id) {
    struct wl_resource* resource = wl_resource_create(client, &applestia_glass_shapes_manager_v1_interface, (int)version, id);
    if (!resource) {
        wl_client_post_no_memory(client);
        return;
    }
    struct manager_object* manager = calloc(1, sizeof(*manager));
    if (!manager) {
        wl_resource_destroy(resource);
        wl_client_post_no_memory(client);
        return;
    }
    manager->resource = resource;
    manager->inert = data != g_global;
    wl_list_insert(&g_managers, &manager->link);
    wl_resource_set_implementation(resource, &manager_impl, manager, manager_destroy);
}
static int api_start(struct wl_display* display) {
    ensure_lists();
    if (g_global)
        return 0;
    // Same retired-global pattern as item-helper: tolerate binds already in
    // flight at withdrawal, reclaim at the next plugin generation's start.
    struct retired_global *retired, *tmp;
    wl_list_for_each_safe(retired, tmp, &g_retired, link) {
        wl_global_destroy(retired->global);
        wl_list_remove(&retired->link);
        free(retired);
    }
    // Allocate retirement storage up front; stop must not have an OOM path
    // that destroys a global which an in-flight bind still targets.
    g_global_retirement = calloc(1, sizeof(*g_global_retirement));
    if (!g_global_retirement)
        return -1;
    g_global = wl_global_create(display, &applestia_glass_shapes_manager_v1_interface, 1, NULL, bind_manager);
    if (!g_global) {
        free(g_global_retirement);
        g_global_retirement = NULL;
        return -1;
    }
    wl_global_set_user_data(g_global, g_global);
    return 0;
}
static void api_stop(void) {
    // Defense in depth: no pointers into an unloaded plugin survive stop.
    g_userdata = NULL; g_created = g_destroyed = NULL;
    ensure_lists();
    if (!g_global)
        return;
    struct manager_object* manager;
    wl_list_for_each(manager, &g_managers, link) {
        if (!manager->inert)
            applestia_glass_shapes_manager_v1_send_finished(manager->resource);
        manager->inert = 1;
    }
    struct shapes_object* shapes;
    wl_list_for_each(shapes, &g_shapes, link) {
        if (shapes->listening)
            wl_list_remove(&shapes->surface_destroy_listener.link);
        shapes->listening = 0;
        shapes->inert = 1;
        shapes->surface = NULL;
    }
    wl_global_remove(g_global);
    g_global_retirement->global = g_global;
    wl_list_insert(&g_retired, &g_global_retirement->link);
    g_global_retirement = NULL;
    g_global = NULL;
}
static void api_set_callbacks(void* userdata, applestia_shapes_surface_fn created, applestia_shapes_surface_fn destroyed) {
    g_userdata = userdata; g_created = created; g_destroyed = destroyed;
}
static int api_get_pending(struct wl_resource* surface, struct applestia_shapes_state* out) {
    ensure_lists();
    struct shapes_object* shapes = find_shapes(surface);
    if (!shapes)
        return 0;
    *out = shapes->pending;
    return 1;
}
static void api_apply_state(struct wl_resource* surface, const struct applestia_shapes_state* state) {
    ensure_lists();
    struct shapes_object* shapes = find_shapes(surface);
    if (shapes && memcmp(&shapes->current, state, sizeof(shapes->current))) {
        shapes->current = *state;
        shapes->generation = ++g_generation;
    }
}
static int api_get(struct wl_resource* surface, struct applestia_shapes_state* out) {
    ensure_lists();
    struct shapes_object* shapes = find_shapes(surface);
    if (!shapes)
        return 0;
    *out = shapes->current;
    return 1;
}
static uint64_t api_generation(struct wl_resource* surface) {
    ensure_lists();
    struct shapes_object* shapes = find_shapes(surface);
    // Absence is a state change too: do not reset to zero on object destroy.
    // A new association gets a still newer revision, preventing ABA cache hits.
    return shapes ? shapes->generation : g_generation;
}
static void api_for_each(applestia_shapes_surface_fn callback, void* userdata) {
    ensure_lists();
    struct shapes_object* shapes;
    wl_list_for_each(shapes, &g_shapes, link)
        if (!shapes->inert && shapes->surface)
            callback(shapes->surface, userdata);
}
const struct applestia_shapes_helper_v1_api applestia_shapes_helper_v1_api = {
    .abi_version = APPLESTIA_SHAPES_HELPER_ABI_VERSION,
    .start = api_start, .stop = api_stop, .set_callbacks = api_set_callbacks,
    .get_pending = api_get_pending, .apply_state = api_apply_state, .get = api_get,
    .generation = api_generation, .for_each_active = api_for_each,
};
