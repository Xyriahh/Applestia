#include "GlassShapes.hpp"
#include "Globals.hpp"
#include "shapes-helper/applestia_shapes_helper_api.h"
#include "shapes-helper/applestia_shapes_loader_path.h"
#include "shapes-helper/CommitSnapshots.hpp"

#include <hyprland/src/Compositor.hpp>
#include <hyprland/src/helpers/Color.hpp>
#include <hyprland/src/plugins/PluginAPI.hpp>
#include <hyprland/src/protocols/core/Compositor.hpp>
#include <hyprutils/signal/Listener.hpp>

#include <cerrno>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <dlfcn.h>
#include <fcntl.h>
#include <format>
#include <sys/mman.h>
#include <unistd.h>
#include <unordered_map>

extern "C" {
extern const unsigned char applestiaShapesHelperBlobStart[];
extern const unsigned char applestiaShapesHelperBlobEnd[];
}

namespace {
const struct applestia_shapes_helper_v1_api* g_api = nullptr;
bool g_active = false; // loaded API alone is not successful protocol initialization
std::string g_readyPath;
std::string g_errorPath;
char g_failure[1024]{};
const char* g_initStage = "not initialized";
CFunctionHook* g_updateHook = nullptr;
struct SSurfaceListeners {
    Hyprutils::Signal::CHyprSignalListener precommit;
    Hyprutils::Signal::CHyprSignalListener stateCommit;
    Hyprutils::Signal::CHyprSignalListener commit;
    SSurfaceState* pending = nullptr;
    SSurfaceState* current = nullptr;
    CShapeCommitSnapshots<SSurfaceState> snapshots;
};
// Destruction callbacks use only raw wire resources: Hyprland may already
// have released the CWLSurfaceResource before libwayland emits its destroy.
std::unordered_map<wl_resource*, SSurfaceListeners> g_listeners;
std::unordered_map<SSurfaceState*, wl_resource*> g_currentSurfaces;

// Hyprland's forced-eject/init-failure path can dlclose without PLUGIN_EXIT.
// Declared after the state above, so it runs while that state (and our code)
// is still alive. Clean exit is idempotent; forced exit cannot leave callbacks
// in the persistent helper pointing into unmapped plugin text either.
struct SUnloadGuard {
    ~SUnloadGuard() { GlassShapes::exit(); }
} g_unloadGuard;

void warn(std::string_view reason) {
    HyprlandAPI::addNotificationV2(PHANDLE, {
        {"text", std::format("[{}] applestia glass shapes unavailable ({})", PLUGIN_NAME, reason)},
        {"time", uint64_t{6000}}, {"color", CHyprColor{1.0, 0.8, 0.2, 1.0}},
    });
}
wl_resource* wireResource(CWLSurfaceResource* surface) {
    if (!surface)
        return nullptr;
    auto wrapper = surface->getResource();
    return wrapper ? wrapper->resource() : nullptr;
}
void onCreatedImpl(wl_resource* resource) {
    auto surface = CWLSurfaceResource::fromResource(resource);
    if (!surface || g_listeners.contains(resource))
        return;
    SSurfaceListeners listeners;
    listeners.pending = &surface->m_pending;
    listeners.current = &surface->m_current;
    listeners.precommit = surface->m_events.precommit.listen([resource] {
        auto it = g_listeners.find(resource);
        if (!g_api || it == g_listeners.end())
            return;
        applestia_shapes_state hints{};
        if (g_api->get_pending(resource, &hints))
            it->second.snapshots.capturePending(hints);
    });
    listeners.stateCommit = surface->m_events.stateCommit.listen([resource](WP<SSurfaceState> state) {
        auto it = g_listeners.find(resource);
        if (!g_api || it == g_listeners.end())
            return;
        try {
            it->second.snapshots.enqueue(state);
        } catch (...) {
            GlassShapes::recordFailure("queued commit snapshot allocation failed");
            GlassShapes::exit();
        }
    });
    listeners.commit = surface->m_events.commit.listen([resource] {
        auto it = g_listeners.find(resource);
        if (!g_api || it == g_listeners.end())
            return;
        if (const auto* hints = it->second.snapshots.staged()) {
            g_api->apply_state(resource, hints);
            it->second.snapshots.published();
        }
    });
    g_currentSurfaces.emplace(listeners.current, resource);
    g_listeners.emplace(resource, std::move(listeners));
}
void onCreated(wl_resource* resource, void*) {
    try {
        onCreatedImpl(resource);
    } catch (...) {
        GlassShapes::recordFailure("surface shape listener/snapshot allocation failed");
        GlassShapes::exit();
    }
}
void onDestroyed(wl_resource* resource, void*) {
    if (auto it = g_listeners.find(resource); it != g_listeners.end())
        g_currentSurfaces.erase(it->second.current);
    g_listeners.erase(resource);
}
using updateFromFn = void (*)(SSurfaceState*, SSurfaceState&);
void hkUpdateFrom(SSurfaceState* current, SSurfaceState& state) {
    ((updateFromFn)g_updateHook->m_original)(current, state);
    if (!g_active)
        return;
    auto surface = g_currentSurfaces.find(current);
    if (surface == g_currentSurfaces.end())
        return;
    auto it = g_listeners.find(surface->second);
    if (it != g_listeners.end())
        it->second.snapshots.updated(state, it->second.pending);
}
bool installStateHook() {
    // Matching Hyprland calls this out-of-line from commitState immediately
    // before any commit event, including null-buffer and synchronized updates.
    // Generic commit signals alone cannot identify queued A versus queued B.
    for (const auto& match : HyprlandAPI::findFunctionsByName(PHANDLE, "SSurfaceState10updateFrom")) {
        if (match.demangled != "SSurfaceState::updateFrom(SSurfaceState&)")
            continue;
        g_updateHook = HyprlandAPI::createFunctionHook(PHANDLE, match.address, (void*)hkUpdateFrom);
        if (g_updateHook && g_updateHook->hook())
            return true;
        if (g_updateHook)
            HyprlandAPI::removeFunctionHook(PHANDLE, g_updateHook);
        g_updateHook = nullptr;
        GlassShapes::recordFailure("SSurfaceState::updateFrom hook failed (possibly owned by another plugin)");
        return false;
    }
    GlassShapes::recordFailure("SSurfaceState::updateFrom symbol missing; applied commit identity unavailable");
    return false;
}
void systemFailure(const char* operation, int error) {
    char message[1024];
    std::snprintf(message, sizeof(message), "%s: %s (errno %d)", operation, std::strerror(error), error);
    GlassShapes::recordFailure(message);
}
bool loadHelper() {
    if (void* adopted = dlsym(RTLD_DEFAULT, "applestia_shapes_helper_v1_api")) {
        g_api = static_cast<const struct applestia_shapes_helper_v1_api*>(adopted);
        return true;
    }
    const auto size = static_cast<size_t>(applestiaShapesHelperBlobEnd - applestiaShapesHelperBlobStart);
    int fd = memfd_create("applestia-shapes-helper", MFD_CLOEXEC);
    if (fd < 0) {
        systemFailure("memfd_create", errno);
        return false;
    }
    size_t written = 0;
    while (written < size) {
        const auto n = write(fd, applestiaShapesHelperBlobStart + written, size - written);
        if (n < 0 && errno == EINTR)
            continue;
        if (n <= 0) {
            systemFailure("write embedded helper", n < 0 ? errno : EIO);
            close(fd);
            return false;
        }
        written += static_cast<size_t>(n);
    }
    char path[64];
    applestia_shapes_loader_path(path, sizeof(path), fd);
    void* handle = dlopen(path, RTLD_NOW | RTLD_GLOBAL | RTLD_NODELETE);
    close(fd);
    if (!handle) {
        const char* error = dlerror();
        GlassShapes::recordFailure(error ? error : "dlopen helper failed without loader diagnostic");
        return false;
    }
    dlerror();
    g_api = static_cast<const struct applestia_shapes_helper_v1_api*>(dlsym(handle, "applestia_shapes_helper_v1_api"));
    if (!g_api) {
        const char* error = dlerror();
        GlassShapes::recordFailure(error ? error : "helper API symbol missing");
    }
    // Intentionally never dlclose: libwayland keeps interfaces/handlers alive.
    return g_api != nullptr;
}
void withdrawReady() {
    if (!g_readyPath.empty())
        unlink(g_readyPath.c_str());
}
void advertiseReady() {
    if (g_readyPath.empty())
        return;
    int fd = open(g_readyPath.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC | O_NOFOLLOW, 0600);
    if (fd < 0) {
        systemFailure("readiness marker open", errno);
        return; // registry activation, not this advisory marker, is authoritative
    }
    const bool ok = write(fd, "1\n", 2) == 2;
    const int error = errno;
    close(fd);
    if (!ok) {
        systemFailure("readiness marker write", error);
        withdrawReady();
    }
}
}

bool GlassShapes::init() {
    if (g_active)
        return true;
    try {
        g_initStage = "readiness path setup";
        const char* runtime = std::getenv("XDG_RUNTIME_DIR");
        g_readyPath = runtime ? std::string(runtime) + "/applestia-shapes-" + g_pCompositor->m_instanceSignature + ".ready" : "";
        g_errorPath = runtime ? std::string(runtime) + "/applestia-shapes-" + g_pCompositor->m_instanceSignature + ".error" : "";
        withdrawReady(); // remove any stale readiness, including on failure
        g_failure[0] = '\0';
        if (!g_errorPath.empty())
            unlink(g_errorPath.c_str());
        g_initStage = "persistent helper load/adoption";
        if (!loadHelper()) {
            warn(lastFailure());
            return false;
        }
        if (g_api->abi_version != APPLESTIA_SHAPES_HELPER_ABI_VERSION) {
            char message[128];
            std::snprintf(message, sizeof(message), "helper ABI mismatch: loaded %u, expected %u", g_api->abi_version, APPLESTIA_SHAPES_HELPER_ABI_VERSION);
            recordFailure(message);
            g_api = nullptr; // do not call an incompatible API layout
            warn(lastFailure());
            return false;
        }
        // Adoption never preserves the previous generation's plugin callbacks.
        g_api->set_callbacks(nullptr, nullptr, nullptr);
        g_initStage = "applied state identity hook";
        if (!installStateHook()) {
            GlassShapes::exit();
            warn(lastFailure());
            return false;
        }
        g_initStage = "wayland global creation";
        if (g_api->start(g_pCompositor->m_wlDisplay) != 0) {
            recordFailure("wayland global creation failed (allocation or wl_global_create)");
            GlassShapes::exit();
            warn(lastFailure());
            return false;
        }
        g_api->set_callbacks(nullptr, &onCreated, &onDestroyed);
        g_initStage = "surface listener recovery";
        g_api->for_each_active(&onCreated, nullptr);
        if (!g_api)
            return false; // listener recovery caught an error and withdrew the protocol
        g_active = true; // all potentially throwing initialization is complete
        advertiseReady();
        g_initStage = "active";
        return true;
    } catch (const std::exception& error) {
        char message[1024];
        std::snprintf(message, sizeof(message), "initialization exception during %s: %s", g_initStage, error.what());
        recordFailure(message);
        GlassShapes::exit();
        return false;
    } catch (...) {
        // PLUGIN_INIT failure may unload us without PLUGIN_EXIT. Never leave
        // callable plugin pointers or an advertised global/marker in that case.
        char message[256];
        std::snprintf(message, sizeof(message), "unknown initialization exception during %s", g_initStage);
        recordFailure(message);
        GlassShapes::exit();
        return false;
    }
}
void GlassShapes::exit() {
    g_active = false;
    withdrawReady();
    if (g_api) {
        g_api->set_callbacks(nullptr, nullptr, nullptr);
        g_api->stop();
    }
    g_listeners.clear();
    g_currentSurfaces.clear();
    g_api = nullptr;
    if (g_updateHook) {
        HyprlandAPI::removeFunctionHook(PHANDLE, g_updateHook);
        g_updateHook = nullptr;
    }
}
bool GlassShapes::active() { return g_active; }
void GlassShapes::recordFailure(std::string_view reason) noexcept {
    const auto length = std::min(reason.size(), sizeof(g_failure) - 1);
    if (length)
        std::memmove(g_failure, reason.data(), length);
    g_failure[length] = '\0';
    if (g_errorPath.empty())
        return;
    int fd = open(g_errorPath.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC | O_NOFOLLOW, 0600);
    if (fd < 0)
        return;
    (void)write(fd, g_failure, length);
    (void)write(fd, "\n", 1);
    close(fd);
}
std::string_view GlassShapes::lastFailure() noexcept { return g_failure; }

std::optional<std::vector<GlassShapes::SGlassShape>> GlassShapes::forSurface(CWLSurfaceResource* surface) {
    auto* resource = wireResource(surface);
    if (!g_active || !g_api || !resource)
        return std::nullopt;
    applestia_shapes_state state{};
    if (!g_api->get(resource, &state))
        return std::nullopt;
    std::vector<SGlassShape> shapes;
    shapes.reserve(state.count);
    for (uint32_t i = 0; i < state.count; ++i) {
        const auto& s = state.shapes[i];
        SGlassShape shape;
        shape.x = s.x; shape.y = s.y; shape.width = s.width; shape.height = s.height;
        shape.radii = {s.radii[0], s.radii[1], s.radii[2], s.radii[3]};
        shape.depth = s.depth; shape.tint = s.tint; shape.preset = s.preset;
        shape.opacity = s.opacity;
        if (s.has_clip)
            shape.clip = std::array<float, 4>{s.clip[0], s.clip[1], s.clip[2], s.clip[3]};
        shapes.push_back(std::move(shape));
    }
    return shapes;
}
uint64_t GlassShapes::generationForSurface(CWLSurfaceResource* surface) {
    auto* resource = wireResource(surface);
    return g_active && g_api && resource ? g_api->generation(resource) : 0;
}
