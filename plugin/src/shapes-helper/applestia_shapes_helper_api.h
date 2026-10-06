#ifndef APPLESTIA_SHAPES_HELPER_API_H
#define APPLESTIA_SHAPES_HELPER_API_H

// C-only ABI: this library outlives the reloadable compositor plugin.
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

struct wl_display;
struct wl_resource;
#define APPLESTIA_SHAPES_HELPER_ABI_VERSION 2u
#define APPLESTIA_SHAPES_CAP 128u
#define APPLESTIA_SHAPES_PRESET_CAP 128u

struct applestia_shape {
    float x, y, width, height;
    float radii[4];
    uint32_t depth, tint;
    char preset[APPLESTIA_SHAPES_PRESET_CAP];
    float opacity;
    uint32_t has_clip;
    float clip[4];
};
struct applestia_shapes_state {
    uint32_t count;
    struct applestia_shape shapes[APPLESTIA_SHAPES_CAP];
};

typedef void (*applestia_shapes_surface_fn)(struct wl_resource*, void*);
struct applestia_shapes_helper_v1_api {
    uint32_t abi_version;
    int (*start)(struct wl_display*);
    // Also clears all plugin callbacks, even when already stopped.
    void (*stop)(void);
    void (*set_callbacks)(void*, applestia_shapes_surface_fn created, applestia_shapes_surface_fn destroyed);
    // Caller associates this copy with the real compositor queued-state identity.
    int (*get_pending)(struct wl_resource*, struct applestia_shapes_state*);
    // Publish only the snapshot of the state actually applied to the foreground.
    void (*apply_state)(struct wl_resource*, const struct applestia_shapes_state*);
    // 1 even for an empty list; 0 for no active object.
    int (*get)(struct wl_resource*, struct applestia_shapes_state*);
    uint64_t (*generation)(struct wl_resource*);
    void (*for_each_active)(applestia_shapes_surface_fn, void*);
};
extern const struct applestia_shapes_helper_v1_api applestia_shapes_helper_v1_api;

#ifdef __cplusplus
}
#endif
#endif
