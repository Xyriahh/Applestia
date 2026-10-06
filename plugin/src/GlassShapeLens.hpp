#pragma once

#include <algorithm>
#include <array>
#include <cmath>

// One physical control material, in logical pixels until geometry() scales it.
// Presets may change frost/tint/specular, never any of these optical parameters.
// Tuned by eye; supersedes the original conservative .6*clamp(.5*r,4,16) lens:
// ordinary radius-16 cards were only pulling ~4.8px vs the outer panel's ~22px.
namespace GlassShapeLens {
inline constexpr float RADIUS_TO_BEZEL = 1.5f;
inline constexpr float MIN_BEZEL_LOGICAL = 12.0f;
inline constexpr float MAX_BEZEL_LOGICAL = 24.0f;
inline constexpr float SHORT_SIDE_BEZEL_CAP = 0.35f; // Centre remains flat, including small pills.
inline constexpr float IOR_DISPLACEMENT = 0.9f; // Fixed material; bounded excursion <= bezel.
inline constexpr float CHROMATIC_LOGICAL = 0.25f;
inline constexpr float LIP_LOGICAL = 1.5f;
inline constexpr float FROST_BASE_RADIUS_LOGICAL = 0.5f;
inline constexpr float FROST_RADIUS_RANGE_LOGICAL = 2.0f;
inline constexpr float SAMPLE_GUARD_PX = 2.0f; // Bilinear/AA/filtered profile footprint.
// A lens band never reaches into a contained child glass element: on that side
// it ends CHILD_CLEARANCE before the child. Otherwise the parent's warp right
// next to the child's edge masks the child's own edge warp (recorder pill top
// sat 23px below a 24px card band and read as "unwarped"). Other sides keep
// the full shared strength; displacement scales with the per-side band.
inline constexpr float CHILD_CLEARANCE_LOGICAL = 6.0f;
inline constexpr float MIN_SIDE_BEZEL_LOGICAL = 6.0f;

struct SGeometry {
    float bezelPx, displacementPx, chromaticPx, lipPx;
};
inline SGeometry geometry(float width, float height, const std::array<float, 4>& radii, float scale) {
    const float shortSide = std::max(0.0f, std::min(width, height));
    const float radius = std::max(0.0f, std::min({radii[0], radii[1], radii[2], radii[3], shortSide * 0.5f}));
    const float bezel = std::min(std::clamp(RADIUS_TO_BEZEL * radius, MIN_BEZEL_LOGICAL, MAX_BEZEL_LOGICAL),
                                 SHORT_SIDE_BEZEL_CAP * shortSide) * scale;
    return {bezel, IOR_DISPLACEMENT * bezel, CHROMATIC_LOGICAL * scale, LIP_LOGICAL * scale};
}
// Per-side bands (top, right, bottom, left) in px from logical child gaps.
// gaps: smallest logical distance from this shape's edge to a contained child
// on that side (negative/huge = no child).
inline std::array<float, 4> sideBezels(float bezelPx, const std::array<float, 4>& gaps, float scale) {
    std::array<float, 4> out{};
    for (int i = 0; i < 4; ++i) {
        float b = bezelPx;
        if (gaps[i] < 1e8f)
            b = std::min(b, std::max(MIN_SIDE_BEZEL_LOGICAL, gaps[i] - CHILD_CLEARANCE_LOGICAL) * scale);
        out[i] = std::max(0.0f, b);
    }
    return out;
}
inline float frostRadiusPx(float frost, float scale) {
    return (FROST_BASE_RADIUS_LOGICAL + FROST_RADIUS_RANGE_LOGICAL * frost) * scale;
}
inline float samplePaddingPx(const SGeometry& geometry, float frost, float scale) {
    return std::ceil(geometry.displacementPx + geometry.chromaticPx + frostRadiusPx(frost, scale) + SAMPLE_GUARD_PX);
}
inline float maximumSamplePaddingPx(float scale) {
    const SGeometry maximum{MAX_BEZEL_LOGICAL * scale, IOR_DISPLACEMENT * MAX_BEZEL_LOGICAL * scale,
                             CHROMATIC_LOGICAL * scale, LIP_LOGICAL * scale};
    return samplePaddingPx(maximum, 1.0f, scale);
}
}
