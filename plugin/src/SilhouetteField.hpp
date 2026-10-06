#pragma once

#include "GlassRenderer.hpp"
#include "PluginConfig.hpp"

#include <GLES3/gl32.h>
#include <array>
#include <hyprland/src/render/Framebuffer.hpp>

// Signed distance field of a layer's rendered silhouette, for the silhouette
// shape mode. Built from the layer's temp FBO alpha with a jump flood (after
// ShojiWM's island glass pipeline), at a fraction of framebuffer resolution and
// clamped to the rim width: the interior past the rim is flat glass and needs
// no distance, which keeps the flood to a handful of passes.
//
// The field covers exactly the layer's transformed box, so field UV equals the
// glass quad's own UV and the glass shader samples it at v_texcoord.
//
// Owns raw GL textures/FBOs (RGBA16F) rather than Hyprland framebuffers: the
// passes need float storage independent of the monitor's format.
class CSilhouetteField {
  public:
    CSilhouetteField() = default;
    ~CSilhouetteField();

    CSilhouetteField(const CSilhouetteField&)            = delete;
    CSilhouetteField& operator=(const CSilhouetteField&) = delete;

    struct SInput {
        GLuint   maskTexture = 0; // layer temp FBO texture (premultiplied surface)
        Vector2D maskUVOffset;    // field UV -> temp FBO UV
        Vector2D maskUVScale;
        Vector2D boxSizePx;       // layer transformBox size, framebuffer px
        float    threshold    = 0.08f;
        float    monitorScale = 1.0f;
        const GlassRenderer::SRegionRect* regionRects     = nullptr; // box-local px, nullptr/0 = no gating
        int                               regionRectCount = 0;
        // debug:verify reference: compute every texel of the field, no restriction.
        bool reference = false;
    };

    // Rebuilds when the layer content changed, the box/options changed, or no
    // field exists yet. Returns true when a valid field is available afterwards;
    // `rebuilt` reports whether passes ran this call. Restores the caller's
    // framebuffer and viewport.
    bool update(const SInput& input, const SLayerSilhouetteOptions& options, bool contentChanged,
                SP<Render::IFramebuffer> callerFramebuffer, bool& rebuilt);

    [[nodiscard]] GLuint texture() const noexcept { return m_valid ? m_textures[RESULT] : 0; }
    // Field texels the last update() submitted (0 when it reused the field).
    [[nodiscard]] double lastBuiltTexels() const noexcept { return m_lastBuiltTexels; }
    // Indirect draw buffer listing the 16x16 box-px tiles with any coverage, or 0
    // (draw the whole quad). Valid for the field returned by texture().
    [[nodiscard]] GLuint tileBuffer() const noexcept { return m_valid && m_tilesValid ? m_tileBuffer : 0; }
    static constexpr float TILE_SIZE_PX = 16.0f;
    static bool s_tileListEnabled; // debug:tiles = 0 turns tile lists off
    // Outcome of the last conditional build once the GPU has it: 0 = skipped
    // (mask unchanged), 1 = rebuilt, -1 = none pending / not available yet.
    [[nodiscard]] int pollConditionalOutcome();
    // Framebuffer px per field px (1 / options.scale, adjusted for rounding).
    [[nodiscard]] float  fieldToFramebuffer() const noexcept { return m_fieldToFramebuffer; }
    // Rim width the field was built with, in framebuffer px.
    [[nodiscard]] float  rimWidthPx() const noexcept { return m_rimWidthPx; }

    void release() noexcept;

    // Field texels the glass shader reads (region rects + Sobel reach), as of the last update().
    [[nodiscard]] const CRegion& interest() const noexcept { return m_interest; }
    // debug:verify: texels of `area` whose distance differs between two fields of equal size.
    // -1 when they cannot be compared. Reads back through the CPU; restores the caller's framebuffer.
    static int64_t mismatches(const CSilhouetteField& a, const CSilhouetteField& b, const CRegion& area, SP<Render::IFramebuffer> callerFramebuffer);

  private:
    // 0: coverage mask, 1/2: ping-pong (jump flood, then distance + smoothing),
    // 3: the field, 4: the mask the field was built from. Every pass runs only
    // where a later pass (or the glass shader) reads it, so 0-2 hold stale
    // texels elsewhere.
    //
    // Content commits rebuild conditionally: the GPU compares the new mask with
    // texture 4, and every later pass is an indirect draw whose instance count
    // only that comparison can raise from 0. The field is a function of the mask
    // and the build parameters alone, so a skipped build leaves exactly the
    // field a rebuild would produce. No CPU readback, no GPU wait.
    static constexpr int RESULT = 3, PREVIOUS_MASK = 4;
    std::array<GLuint, 5> m_textures{};
    std::array<GLuint, 5> m_framebuffers{};
    GLuint m_changeQuery = 0;   // diagnostics only: did the GPU rebuild? read when ready, never waited for
    bool   m_queryPending = false;
    GLuint m_drawArgs = 0;      // indirect draw + dispatch arguments, raised by the change test
    // Partial rebuilds: per field tile (8x8 texels) the generation it last changed
    // in, its row distance to a change, and one draw list per field pass.
    GLuint   m_dirtyBuffer = 0, m_rowDistBuffer = 0, m_listBuffer = 0;
    int      m_fieldTiles = 0, m_listPasses = 0;
    uint32_t m_generation = 0;
    // Panel tile list (see silhouette_tiles.comp): {vertexCount, instanceCount, first, baseInstance, tiles[]}
    GLuint m_tileBuffer = 0;
    int    m_tilesX = 0, m_tilesY = 0;
    bool   m_tilesValid = false;
    int   m_width = 0, m_height = 0;
    bool  m_valid  = false;
    double m_lastBuiltTexels = 0.0;
    CRegion m_interest;
    float m_fieldToFramebuffer = 2.0f;
    float m_rimWidthPx         = 0.0f;

    // Inputs of the last build, compared to detect anything that changes the field
    Vector2D                m_lastBoxSize;
    SLayerSilhouetteOptions m_lastOptions;
    float                   m_lastThreshold    = -1.0f;
    float                   m_lastMonitorScale = -1.0f;
    int                     m_lastRectCount    = -1;
    std::array<GlassRenderer::SRegionRect, GlassRenderer::MAX_REGION_RECTS> m_lastRects{};

    bool ensureStorage(int width, int height);
};
