#pragma once

#include "GlassRenderer.hpp"
#include <hyprland/src/Compositor.hpp>
#include <chrono>
#include <fstream>
#include <iomanip>
#include <vector>
#include <sys/stat.h>
#include <unistd.h>

// Versioned, bounded geometry-only bridge for Applestia. No commands or
// texture transfers. The client uses atomic replacement and a heartbeat.
namespace InnerGlass {
inline std::string readyPath() {
    const char* runtime = std::getenv("XDG_RUNTIME_DIR");
    return runtime ? std::string(runtime) + "/applestia-inner-" + g_pCompositor->m_instanceSignature + ".ready" : "";
}

inline void advertise() {
    const auto path = readyPath();
    if (!path.empty()) { std::ofstream file(path); file << "1\n"; }
}

inline void withdraw() {
    const auto path = readyPath();
    if (!path.empty()) unlink(path.c_str());
}

struct Region {
    std::string screen;
    std::array<float, 4> box{}, radii{}, clip{};
    float opacity = 0;
};

inline void populate(GlassRenderer::SMaskInfo& mask, const std::string& screen, float scale) {
    static std::vector<Region> cached;
    static int64_t timestamp = 0;
    static timespec modified{};
    static auto checked = std::chrono::steady_clock::time_point{};
    const auto now = std::chrono::steady_clock::now();
    if (now - checked >= std::chrono::milliseconds(25)) {
        checked = now;
        const char* runtime = std::getenv("XDG_RUNTIME_DIR");
        if (!runtime) return;
        const auto path = std::string(runtime) + "/applestia-inner-" + g_pCompositor->m_instanceSignature + ".regions";
        struct stat st{};
        if (lstat(path.c_str(), &st) != 0 || !S_ISREG(st.st_mode) || st.st_uid != getuid() || st.st_size > 131072) {
            cached.clear();
            modified = {};
        } else if (st.st_mtim.tv_sec != modified.tv_sec || st.st_mtim.tv_nsec != modified.tv_nsec) {
            std::ifstream file(path);
            int version = 0, count = 0;
            int64_t nextTimestamp = 0;
            std::vector<Region> next;
            bool valid = bool(file >> version >> nextTimestamp >> count) && version == 1 && count >= 0 && count <= 512;
            for (int i = 0; valid && i < count; ++i) {
                Region r;
                valid = bool(file >> std::quoted(r.screen)) && r.screen.size() < 128;
                for (auto* values : {&r.box, &r.radii, &r.clip})
                    for (auto& value : *values)
                        valid = bool(file >> value) && std::isfinite(value) && std::abs(value) <= 32768 && valid;
                valid = bool(file >> r.opacity) && std::isfinite(r.opacity) && valid;
                if (valid && r.box[2] >= 2 && r.box[3] >= 2 && r.opacity >= 0.04f)
                    next.push_back(std::move(r));
            }
            if (valid) {
                cached = std::move(next);
                timestamp = nextTimestamp;
                modified = st.st_mtim;
            }
        }
    }
    const auto millis = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    if (timestamp < millis - 3000 || timestamp > millis + 3000) return;
    for (const auto& r : cached) {
        if (r.screen != screen) continue;
        if (mask.innerCount >= 64) break;
        const int i = mask.innerCount++;
        mask.innerBoxes[i] = {r.box[0] * scale, r.box[1] * scale, r.box[2] * scale, r.box[3] * scale};
        for (int j = 0; j < 4; ++j) {
            mask.innerRadii[i][j] = std::max(0.0f, r.radii[j] * scale);
            mask.innerClips[i][j] = r.clip[j] * scale;
        }
        mask.innerOpacities[i] = std::clamp(r.opacity, 0.0f, 1.0f);
    }
}
}
