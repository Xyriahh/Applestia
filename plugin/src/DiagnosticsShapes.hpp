#pragma once

#include "GlassShapes.hpp"
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace Diagnostics {
inline constexpr size_t MAX_SHAPE_DIAGNOSTIC_LAYERS = 16;
inline constexpr size_t MAX_SHAPE_DIAGNOSTIC_SHAPES = 128;
inline constexpr size_t MAX_SHAPE_DIAGNOSTIC_STRING = 256;

// Command-time APPLIED snapshots only. No frame cache or render hot-path writes.
struct SAppliedLayerShapes {
    std::string layer, nameSpace, monitor, surface;
    std::array<double, 4> layerBoxGlobal{};
    std::array<double, 2> monitorPosition{};
    double monitorScale = 1;
    int monitorTransform = 0;
    uint64_t generation = 0;
    bool mapped = false;
    bool renderingEligible = false;
    std::optional<std::vector<GlassShapes::SGlassShape>> applied;
};

// Pure formatter, independently testable without a compositor or GL context.
// totalLayers includes omitted live layers. Native-bound entries are prioritized.
std::string formatAppliedShapes(const std::vector<SAppliedLayerShapes>& layers, bool protocolActive,
                                size_t totalLayers, bool json);
}
