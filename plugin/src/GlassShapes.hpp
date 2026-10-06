#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

class CWLSurfaceResource;

namespace GlassShapes {
struct SGlassShape {
    float x = 0, y = 0, width = 0, height = 0;
    std::array<float, 4> radii{}; // top-left, top-right, bottom-right, bottom-left
    uint32_t depth = 0;
    uint32_t tint = 0; // RGBA8888
    std::string preset;
    float opacity = 1;
    std::optional<std::array<float, 4>> clip; // surface-local x/y/width/height; flat clip, not a new lens edge
};

bool init();
void exit();
bool active();
// Diagnostics only: records a durable per-instance error without changing
// capability. A renderer may record its downgrade reason before calling exit.
void recordFailure(std::string_view reason) noexcept;
std::string_view lastFailure() noexcept;
// Engaged and empty means a bound object with a committed empty list.
std::optional<std::vector<SGlassShape>> forSurface(CWLSurfaceResource* surface);
// Monotonic revisions for applied changes/association removal. With no object,
// conservatively reports the helper's latest revision; zero if unavailable.
uint64_t generationForSurface(CWLSurfaceResource* surface);
}
