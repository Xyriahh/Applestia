#pragma once

#include <string>
#include <unordered_set>

// Render-thread policy, independent of GL and protocol lifetime internals.
// Restart always goes through init(), which creates a new helper global; it
// never adopts an inert client's old manager/shapes objects.
namespace ShapesCapability {
inline bool eligible(bool layersEnabled, bool hookAvailable,
                     const std::unordered_set<std::string>& include,
                     const std::unordered_set<std::string>& exclude) {
    return layersEnabled && hookAvailable && !exclude.contains("applestia-drawers") &&
           (include.empty() || include.contains("applestia-drawers"));
}

struct SPolicy {
    bool started = false;
    bool fatal = false;

    template <typename Init, typename Exit>
    bool sync(bool allowed, bool active, Init init, Exit exit) {
        if (!started) return active;
        if (!allowed || fatal) {
            if (active) exit();
            return false;
        }
        if (active) return true;
        if (init()) return true;
        fatal = true; // Initialization failure also must not retry every frame.
        return false;
    }
};
}
