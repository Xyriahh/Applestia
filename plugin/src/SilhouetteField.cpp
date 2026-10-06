#include "SilhouetteField.hpp"
#include "Globals.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <vector>
#include <hyprland/src/render/OpenGL.hpp>
#include <hyprland/src/render/Renderer.hpp>
#include <hyprland/src/render/gl/GLFramebuffer.hpp>

// Fullscreen quad projection: maps VAO positions [0,1] to clip space [-1,1]
// (same as GlassRenderer's blur passes, so texcoords map 1:1 onto field texels).
static constexpr std::array<float, 9> FULLSCREEN_PROJECTION = {
    2.0f, 0.0f, 0.0f,
    0.0f, 2.0f, 0.0f,
   -1.0f,-1.0f, 1.0f,
};

static GLuint callerFramebufferId(const SP<Render::IFramebuffer>& framebuffer) {
    return dynamic_cast<Render::GL::CGLFramebuffer*>(framebuffer.get())->getFBID();
}

// GPU-gated rebuilds need GLES 3.1 indirect draws and fragment storage buffers.
namespace {
bool indirectGatingAvailable() {
    static int available = -1;
    if (available < 0) {
        GLint major = 0, minor = 0, blocks = 0;
        glGetIntegerv(GL_MAJOR_VERSION, &major);
        glGetIntegerv(GL_MINOR_VERSION, &minor);
        glGetIntegerv(GL_MAX_FRAGMENT_SHADER_STORAGE_BLOCKS, &blocks);
        available = (major > 3 || (major == 3 && minor >= 1)) && blocks > 0 ? 1 : 0;
    }
    return available == 1;
}
} // namespace

bool CSilhouetteField::s_tileListEnabled = true;

int CSilhouetteField::pollConditionalOutcome() {
    if (!m_queryPending || !m_changeQuery)
        return -1;
    GLuint ready = 0;
    glGetQueryObjectuiv(m_changeQuery, GL_QUERY_RESULT_AVAILABLE, &ready);
    if (!ready)
        return -1;
    GLuint changed = 0;
    glGetQueryObjectuiv(m_changeQuery, GL_QUERY_RESULT, &changed);
    m_queryPending = false;
    return changed ? 1 : 0;
}

CSilhouetteField::~CSilhouetteField() {
    release();
}

void CSilhouetteField::release() noexcept {
    // Deleting needs the compositor's GL context; at plugin exit and layer
    // teardown Hyprland's context is current (same as for its own framebuffers).
    if (m_framebuffers[0])
        glDeleteFramebuffers(static_cast<GLsizei>(m_framebuffers.size()), m_framebuffers.data());
    if (m_textures[0])
        glDeleteTextures(static_cast<GLsizei>(m_textures.size()), m_textures.data());
    m_framebuffers = {};
    m_textures     = {};
    if (m_changeQuery)
        glDeleteQueries(1, &m_changeQuery);
    if (m_drawArgs)
        glDeleteBuffers(1, &m_drawArgs);
    m_drawArgs     = 0;
    for (GLuint* buffer : {&m_dirtyBuffer, &m_rowDistBuffer, &m_listBuffer})
        if (*buffer) {
            glDeleteBuffers(1, buffer);
            *buffer = 0;
        }
    m_fieldTiles = m_listPasses = 0;
    m_generation = 0;
    if (m_tileBuffer)
        glDeleteBuffers(1, &m_tileBuffer);
    m_tileBuffer = 0;
    m_tilesX = m_tilesY = 0;
    m_tilesValid = false;
    m_changeQuery  = 0;
    m_queryPending = false;
    m_width = m_height = 0;
    m_valid = false;
}

bool CSilhouetteField::ensureStorage(int width, int height) {
    if (m_textures[0] && width == m_width && height == m_height)
        return true;

    release();

    glGenTextures(static_cast<GLsizei>(m_textures.size()), m_textures.data());
    glGenFramebuffers(static_cast<GLsizei>(m_framebuffers.size()), m_framebuffers.data());

    bool ok = true;
    for (size_t i = 0; i < m_textures.size(); i++) {
        glBindTexture(GL_TEXTURE_2D, m_textures[i]);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, width, height, 0, GL_RGBA, GL_HALF_FLOAT, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

        glBindFramebuffer(GL_FRAMEBUFFER, m_framebuffers[i]);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_textures[i], 0);
        ok = ok && glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
    }
    glBindTexture(GL_TEXTURE_2D, 0);

    if (!ok) {
        release();
        return false;
    }

    m_width  = width;
    m_height = height;
    return true;
}

// Grows every rect of `region` by (ex, ey) and clips to [0, w) x [0, h).
static CRegion expandClipped(const CRegion& region, int ex, int ey, int w, int h) {
    CRegion out;
    for (const auto& r : region.getRects()) {
        const int x1 = std::max(0, r.x1 - ex), y1 = std::max(0, r.y1 - ey);
        const int x2 = std::min(w, r.x2 + ex), y2 = std::min(h, r.y2 + ey);
        if (x2 > x1 && y2 > y1)
            out.add(CBox{double(x1), double(y1), double(x2 - x1), double(y2 - y1)});
    }
    return out;
}

// Box px -> field texels, rounded outward, plus one texel for the mask pass's
// bilinear read of the surface.
static CRegion boxToField(const CRegion& box, double fx, double fy, int w, int h) {
    CRegion out;
    for (const auto& r : box.getRects()) {
        const int x1 = std::max(0, int(std::floor(r.x1 * fx)) - 1), y1 = std::max(0, int(std::floor(r.y1 * fy)) - 1);
        const int x2 = std::min(w, int(std::ceil(r.x2 * fx)) + 1), y2 = std::min(h, int(std::ceil(r.y2 * fy)) + 1);
        if (x2 > x1 && y2 > y1)
            out.add(CBox{double(x1), double(y1), double(x2 - x1), double(y2 - y1)});
    }
    return out;
}

bool CSilhouetteField::update(const SInput& input, const SLayerSilhouetteOptions& options, bool contentChanged,
                              SP<Render::IFramebuffer> callerFramebuffer, bool& rebuilt) {
    rebuilt = false;
    m_lastBuiltTexels = 0.0;

    auto& shaderManager = g_pGlobalState->shaderManager;
    shaderManager.initializeSilhouetteIfNeeded();
    if (!shaderManager.silhouetteReady() || !input.maskTexture || !callerFramebuffer)
        return false;
    if (!(input.boxSizePx.x >= 1.0 && input.boxSizePx.y >= 1.0) || !std::isfinite(input.boxSizePx.x) || !std::isfinite(input.boxSizePx.y))
        return false;

    const int rectCount = std::clamp(input.regionRects ? input.regionRectCount : 0, 0, GlassRenderer::MAX_REGION_RECTS);

    const bool optionsChanged = options.threshold != m_lastOptions.threshold || options.scale != m_lastOptions.scale ||
        options.rimWidth != m_lastOptions.rimWidth || options.normalSmoothing != m_lastOptions.normalSmoothing;
    const bool regionChanged = rectCount != m_lastRectCount ||
        (rectCount > 0 && std::memcmp(m_lastRects.data(), input.regionRects, sizeof(GlassRenderer::SRegionRect) * rectCount) != 0);
    const bool geometryChanged = input.boxSizePx != m_lastBoxSize || input.threshold != m_lastThreshold ||
        input.monitorScale != m_lastMonitorScale || optionsChanged;

    if (m_valid && !contentChanged && !geometryChanged && !regionChanged)
        return true;

    const int width  = std::max(1, static_cast<int>(std::ceil(input.boxSizePx.x * options.scale)));
    const int height = std::max(1, static_cast<int>(std::ceil(input.boxSizePx.y * options.scale)));
    const bool reallocated = !(m_textures[0] && width == m_width && height == m_height);
    if (!ensureStorage(width, height))
        return false;

    m_fieldToFramebuffer = static_cast<float>(input.boxSizePx.x / width);
    m_rimWidthPx         = options.rimWidth * input.monitorScale;
    const float limitFieldPx = m_rimWidthPx / m_fieldToFramebuffer;
    const double fx = width / input.boxSizePx.x, fy = height / input.boxSizePx.y;

    // Pass schedule, and how far each pass reads (texels). Shaders.hpp:
    // seed 1 (4-neighbourhood), jump = its step (texel centred), distance 0,
    // smooth 2*smoothingPx (+1 bilinear) along its axis.
    std::vector<int> jumps;
    {
        int jump = 1;
        while (jump * 2 <= static_cast<int>(std::ceil(limitFieldPx)) + 1)
            jump *= 2;
        for (int step = jump; step >= 1; step /= 2)
            jumps.push_back(step);
        jumps.push_back(1); // one extra unit pass fixes the classic JFA misses
    }
    const bool smoothing = options.normalSmoothing > 0.0f;
    const int  smoothReach = smoothing ? static_cast<int>(std::ceil(2.0f * options.normalSmoothing)) + 1 : 0;
    int jumpReach = 0;
    for (int step : jumps)
        jumpReach += step;
    const int totalReach = 1 + jumpReach + smoothReach;

    // Where the field must be exact: the glass shader reads it only inside the
    // region rects, with its Sobel taps 3 texels out (+1 bilinear, +1 rounding).
    CRegion out;
    if (rectCount > 0) {
        CRegion regionBox;
        for (int i = 0; i < rectCount; i++) {
            const auto& r = input.regionRects[i];
            regionBox.add(CBox{r.x, r.y, r.w, r.h});
        }
        out = expandClipped(boxToField(regionBox, fx, fy, width, height), 5, 5, width, height);
    } else
        out = CRegion(CBox{0, 0, double(width), double(height)});
    m_interest = out.copy();
    if (input.reference)
        out = CRegion(CBox{0, 0, double(width), double(height)});

    (void)totalReach;
    if (out.empty())
        return m_valid;

    // Only the client content may have changed: let the GPU decide whether the
    // mask did. A previous query still in flight is simply replaced.
    const bool conditional = m_valid && !reallocated && !geometryChanged && !regionChanged && !input.reference &&
        indirectGatingAvailable();

    // Tile list: exact only when every field texel is the mean of one 2x2 pixel
    // block (half resolution, even box) and the mask covers the whole field.
    // ...and the mask pass writes every texel (the region spans the whole box):
    // the tile flags are not region-gated, foreground content counts anywhere.
    const auto outRects = out.getRects();
    const bool maskCoversField = outRects.size() == 1 && outRects[0].x1 == 0 && outRects[0].y1 == 0 && outRects[0].x2 == width &&
        outRects[0].y2 == height;
    const bool tileable = maskCoversField && !input.reference && s_tileListEnabled && indirectGatingAvailable() && shaderManager.silhouette.tilesClassify &&
        shaderManager.silhouette.tilesReset && options.scale == 0.5f && width * 2 == static_cast<int>(input.boxSizePx.x) &&
        height * 2 == static_cast<int>(input.boxSizePx.y) && input.boxSizePx.x == std::floor(input.boxSizePx.x) &&
        input.boxSizePx.y == std::floor(input.boxSizePx.y);
    const int tilesX = (width + 7) / 8, tilesY = (height + 7) / 8;
    const GLuint groupsX = static_cast<GLuint>((tilesX + 7) / 8), groupsY = static_cast<GLuint>((tilesY + 7) / 8);
    bool classifyUnconditionally = false;
    if (tileable && (tilesX != m_tilesX || tilesY != m_tilesY || !m_tileBuffer)) {
        if (!m_tileBuffer)
            glGenBuffers(1, &m_tileBuffer);
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, m_tileBuffer);
        glBufferData(GL_SHADER_STORAGE_BUFFER, (4 + size_t(tilesX) * tilesY) * sizeof(GLuint), nullptr, GL_DYNAMIC_DRAW);
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
        m_tilesX = tilesX;
        m_tilesY = tilesY;
        classifyUnconditionally = true;
    }
    if (!tileable)
        m_tilesValid = false;

    // Write regions, from the last pass backwards: each pass writes where the
    // next one reads.
    const CRegion outSmoothH  = smoothing ? expandClipped(out, 0, smoothReach, width, height) : out;
    const CRegion outDistance = smoothing ? expandClipped(outSmoothH, smoothReach, 0, width, height) : out;
    std::vector<CRegion> outJump(jumps.size());
    {
        CRegion next = outDistance;
        for (int i = static_cast<int>(jumps.size()) - 1; i >= 0; i--) {
            outJump[i] = next;
            next = expandClipped(next, jumps[i], jumps[i], width, height);
        }
        // `next` is now what the first jump pass reads: the seed pass's output
    }
    const CRegion outSeed = expandClipped(outJump[0], jumps[0], jumps[0], width, height);
    const CRegion outMask = expandClipped(outSeed, 1, 1, width, height);

    const auto& s = shaderManager.silhouette;

    // Partial rebuild: only field tiles within reach of a changed mask texel are
    // recomputed. A field texel depends on the mask within totalReach of it, so
    // outside that area the new field equals the old one. Pass k must be exact
    // wherever a later pass reads it: its area grows by the reach still to come.
    const int  fieldTilesX = (width + 7) / 8, fieldTilesY = (height + 7) / 8, fieldTiles = fieldTilesX * fieldTilesY;
    const bool partial = conditional && s.partialReady && s.rowDist && s.lists;
    std::vector<int> reaches; // seed, jumps..., distance, [smooth H, smooth V]; then the mask copy
    reaches.push_back(1);
    for (int step : jumps)
        reaches.push_back(step);
    reaches.push_back(0);
    if (smoothing) {
        reaches.push_back(smoothReach);
        reaches.push_back(smoothReach);
    }
    const int passCount = static_cast<int>(reaches.size()) + 1;
    std::array<GLint, 16> thresholds{};
    {
        // In whole tiles: a read reaching r texels can cross ceil(r/8) tile
        // borders, so every pass adds that to what the passes after it need.
        int total = 0;
        for (int r : reaches)
            total += r;
        const int last = static_cast<int>(reaches.size()) - 1;
        thresholds[last] = (total + 7) / 8; // result texels within `total` of a change
        for (int k = last - 1; k >= 0; k--)
            thresholds[k] = thresholds[k + 1] + (reaches[k + 1] + 7) / 8;
        thresholds[passCount - 1] = 0; // the mask copy: changed tiles only
    }
    const GLint maxDist = thresholds[0];

    if (conditional) {
        if (!m_dirtyBuffer || m_fieldTiles != fieldTiles || (partial && (!m_listBuffer || m_listPasses != passCount))) {
            const std::vector<GLuint> zeros(size_t(fieldTiles), 0);
            for (GLuint* buffer : {&m_dirtyBuffer, &m_rowDistBuffer})
                if (!*buffer)
                    glGenBuffers(1, buffer);
            glBindBuffer(GL_SHADER_STORAGE_BUFFER, m_dirtyBuffer);
            glBufferData(GL_SHADER_STORAGE_BUFFER, zeros.size() * sizeof(GLuint), zeros.data(), GL_DYNAMIC_DRAW);
            glBindBuffer(GL_SHADER_STORAGE_BUFFER, m_rowDistBuffer);
            glBufferData(GL_SHADER_STORAGE_BUFFER, zeros.size() * sizeof(GLuint), nullptr, GL_DYNAMIC_DRAW);
            if (partial) {
                if (!m_listBuffer)
                    glGenBuffers(1, &m_listBuffer);
                glBindBuffer(GL_SHADER_STORAGE_BUFFER, m_listBuffer);
                glBufferData(GL_SHADER_STORAGE_BUFFER, size_t(fieldTiles) * passCount * sizeof(GLuint), nullptr, GL_DYNAMIC_DRAW);
                m_listPasses = passCount;
            }
            glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
            m_fieldTiles = fieldTiles;
            m_generation = 0;
        }
        if (++m_generation == 0)
            m_generation = 1; // 0 is the cleared value
    }

    // Every pass overwrites its whole write region (same state discipline as blurBackground()).
    g_pHyprRenderer->blend(false);
    if (glIsEnabled(GL_STENCIL_TEST))
        glDisable(GL_STENCIL_TEST);
    g_pHyprOpenGL->setViewport(0, 0, width, height);

    auto use = [](const SP<CShader>& program) {
        auto shader = g_pHyprOpenGL->useShader(program);
        shader->setUniformMatrix3fv(SHADER_PROJ, 1, GL_FALSE, FULLSCREEN_PROJECTION);
        shader->setUniformInt(SHADER_TEX, 0);
        glBindVertexArray(shader->getUniformLocation(SHADER_SHADER_VAO));
        return shader;
    };
    // Field pass k: the tile-listed program when rebuilding partially.
    auto usePass = [&](const SP<CShader>& full, const SP<CShader>& tiled, int k) -> GLuint {
        const auto& program = partial ? tiled : full;
        use(program);
        if (partial) {
            glUniform2f(glGetUniformLocation(program->program(), "fieldSize"), float(width), float(height));
            glUniform1ui(glGetUniformLocation(program->program(), "listBase"), GLuint(k * fieldTiles));
        }
        return program->program();
    };
    bool gated = false; // later passes are indirect draws gated by the change test
    int  passIndex = -1;
    auto drawInto = [&](int target, GLuint source, const CRegion& region) {
        glBindFramebuffer(GL_FRAMEBUFFER, m_framebuffers[target]);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, source);
        const auto offset = reinterpret_cast<const void*>(uintptr_t(partial && passIndex >= 0 ? (16 + 4 * passIndex) * sizeof(GLuint) : 0));
        for (const auto& r : region.getRects()) {
            g_pHyprOpenGL->scissor(r.x1, r.y1, r.x2 - r.x1, r.y2 - r.y1, false);
            if (gated)
                glDrawArraysIndirect(GL_TRIANGLE_STRIP, offset);
            else
                glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
        }
    };

    // 1. Coverage mask (downsampled, region-gated)
    use(s.mask);
    glUniform2f(s.maskUVOffset, static_cast<float>(input.maskUVOffset.x), static_cast<float>(input.maskUVOffset.y));
    glUniform2f(s.maskUVScale, static_cast<float>(input.maskUVScale.x), static_cast<float>(input.maskUVScale.y));
    glUniform2f(s.maskBoxSizePx, static_cast<float>(input.boxSizePx.x), static_cast<float>(input.boxSizePx.y));
    glUniform1f(s.maskThreshold, input.threshold);
    glUniform1i(s.maskRectCount, rectCount);
    if (rectCount > 0)
        glUniform4fv(s.maskRects, rectCount, reinterpret_cast<const float*>(input.regionRects));
    drawInto(0, input.maskTexture, outMask);

    if (conditional) {
        if (!m_changeQuery)
            glGenQueries(1, &m_changeQuery);
        if (!m_drawArgs)
            glGenBuffers(1, &m_drawArgs);
        // uint offsets: 0-3 draw {count, instanceCount, first, baseInstance}, 4-6 tile-list
        // reset dispatch, 7-9 tile-list dispatch, 10-12 change-area dispatch, 16+4k draw of
        // field pass k. Nothing drawn or dispatched until a texel differs. Fresh storage
        // each build, so the driver never waits for last frame's draws.
        std::vector<GLuint> args(16 + 4 * size_t(passCount), 0);
        args[0] = 4;
        args[5] = args[6] = args[9] = args[12] = 1;
        for (int k = 0; k < passCount; k++)
            args[16 + 4 * k] = 4;
        glBindBuffer(GL_DRAW_INDIRECT_BUFFER, m_drawArgs);
        glBufferData(GL_DRAW_INDIRECT_BUFFER, args.size() * sizeof(GLuint), args.data(), GL_STREAM_DRAW);
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, m_drawArgs);
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, m_dirtyBuffer);

        use(s.diff);
        glUniform1i(s.diffPrevious, 1);
        glUniform2ui(s.diffTileGroups, groupsX, groupsY);
        glUniform2ui(s.diffFieldTileGroups, GLuint((fieldTilesX + 7) / 8), GLuint((fieldTilesY + 7) / 8));
        glUniform1ui(s.diffFieldTilesX, GLuint(fieldTilesX));
        glUniform1ui(s.diffGeneration, m_generation);
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, m_textures[PREVIOUS_MASK]);
        drawInto(1, m_textures[0], outMask); // scratch target; only the arguments and dirty tiles matter
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, 0);
        glMemoryBarrier(GL_COMMAND_BARRIER_BIT | GL_SHADER_STORAGE_BARRIER_BIT);

        if (partial) {
            GLint program = 0;
            glGetIntegerv(GL_CURRENT_PROGRAM, &program); // Hyprland caches it: leave it as found
            glBindBuffer(GL_DISPATCH_INDIRECT_BUFFER, m_drawArgs);
            glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 3, m_listBuffer);
            glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 4, m_rowDistBuffer);
            glUseProgram(s.rowDist);
            glUniform2ui(s.rowDistTiles, GLuint(fieldTilesX), GLuint(fieldTilesY));
            glUniform1ui(s.rowDistGeneration, m_generation);
            glUniform1i(s.rowDistMax, maxDist);
            glDispatchComputeIndirect(10 * sizeof(GLuint));
            glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
            glUseProgram(s.lists);
            glUniform2ui(s.listsTiles, GLuint(fieldTilesX), GLuint(fieldTilesY));
            glUniform1i(s.listsMax, maxDist);
            glUniform1i(s.listsPassCount, passCount);
            glUniform1iv(s.listsThresholds, passCount, thresholds.data());
            glUniform1ui(s.listsStride, GLuint(fieldTiles));
            glDispatchComputeIndirect(10 * sizeof(GLuint));
            glMemoryBarrier(GL_COMMAND_BARRIER_BIT | GL_SHADER_STORAGE_BARRIER_BIT);
            glBindBuffer(GL_DISPATCH_INDIRECT_BUFFER, 0);
            glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 4, 0);
            glUseProgram(program);
        }
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, 0);
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, 0);
        gated = true;
    }

    // 2. Boundary seeds (the first gated pass: its sample count tells, later, whether the GPU rebuilt)
    passIndex = 0;
    usePass(s.seed, s.seedT, passIndex);
    const bool countRebuild = gated && !m_queryPending;
    if (countRebuild)
        glBeginQuery(GL_ANY_SAMPLES_PASSED, m_changeQuery); // diagnostics only, read when ready
    drawInto(1, m_textures[0], outSeed);
    if (countRebuild) {
        glEndQuery(GL_ANY_SAMPLES_PASSED);
        m_queryPending = true;
    }

    // 3. Jump flood, only as far as the rim reaches
    int src = 1;
    for (size_t i = 0; i < jumps.size(); i++) {
        passIndex = 1 + static_cast<int>(i);
        const GLuint program = usePass(s.jump, s.jumpT, passIndex);
        glUniform1f(partial ? glGetUniformLocation(program, "jumpPx") : s.jumpPx, static_cast<float>(jumps[i]));
        const int dst = src == 1 ? 2 : 1;
        drawInto(dst, m_textures[src], outJump[i]);
        src = dst;
    }

    // 4. Signed distance (positive inside), clamped to the rim
    {
        passIndex = 1 + static_cast<int>(jumps.size());
        const GLuint program = usePass(s.distance, s.distanceT, passIndex);
        glUniform1f(partial ? glGetUniformLocation(program, "limitPx") : s.distanceLimitPx, limitFieldPx);
        glUniform1i(partial ? glGetUniformLocation(program, "silhouette") : s.distanceSilhouette, 1);
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, m_textures[0]);
        const int dst = smoothing ? (src == 1 ? 2 : 1) : RESULT;
        drawInto(dst, m_textures[src], outDistance);
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, 0);
        src = dst;
    }

    // 5. Separable smoothing, so gradients (edge normals) are continuous
    if (smoothing) {
        passIndex = 2 + static_cast<int>(jumps.size());
        GLuint program = usePass(s.smooth, s.smoothT, passIndex);
        glUniform1f(partial ? glGetUniformLocation(program, "smoothingPx") : s.smoothPx, options.normalSmoothing);
        glUniform2f(partial ? glGetUniformLocation(program, "axis") : s.smoothAxis, 1.0f, 0.0f);
        const int dst = src == 1 ? 2 : 1;
        drawInto(dst, m_textures[src], outSmoothH);
        passIndex++;
        program = usePass(s.smooth, s.smoothT, passIndex);
        glUniform1f(partial ? glGetUniformLocation(program, "smoothingPx") : s.smoothPx, options.normalSmoothing);
        glUniform2f(partial ? glGetUniformLocation(program, "axis") : s.smoothAxis, 0.0f, 1.0f);
        drawInto(RESULT, m_textures[dst], out);
    }

    // 6. The mask this field belongs to, for the next change test
    passIndex = passCount - 1;
    usePass(s.copy, s.copyT, passIndex);
    drawInto(PREVIOUS_MASK, m_textures[0], outMask);
    passIndex = -1;
    if (partial)
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 3, 0);

    g_pHyprOpenGL->scissor(nullptr);

    // 7. Tile list for the panel draw, from the new mask (gated like the passes above)
    if (tileable && (!m_tilesValid || gated || classifyUnconditionally || !conditional)) {
        GLint program = 0;
        glGetIntegerv(GL_CURRENT_PROGRAM, &program); // Hyprland caches it: leave it as found
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, m_tileBuffer);
        const bool indirect = gated && m_tilesValid && !classifyUnconditionally;
        if (indirect)
            glBindBuffer(GL_DISPATCH_INDIRECT_BUFFER, m_drawArgs);
        glUseProgram(s.tilesReset);
        if (indirect)
            glDispatchComputeIndirect(4 * sizeof(GLuint));
        else
            glDispatchCompute(1, 1, 1);
        glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
        glUseProgram(s.tilesClassify);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, m_textures[0]);
        glUniform1i(s.tilesMask, 0);
        glUniform2ui(s.tilesCount, static_cast<GLuint>(tilesX), static_cast<GLuint>(tilesY));
        if (indirect)
            glDispatchComputeIndirect(7 * sizeof(GLuint));
        else
            glDispatchCompute(groupsX, groupsY, 1);
        glMemoryBarrier(GL_COMMAND_BARRIER_BIT | GL_SHADER_STORAGE_BARRIER_BIT);
        if (indirect)
            glBindBuffer(GL_DISPATCH_INDIRECT_BUFFER, 0);
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, 0);
        glUseProgram(program);
        m_tilesValid = true;
    }
    if (gated)
        glBindBuffer(GL_DRAW_INDIRECT_BUFFER, 0);

    for (const auto& r : out.getRects())
        m_lastBuiltTexels += double(r.x2 - r.x1) * double(r.y2 - r.y1);

    m_valid  = true;
    rebuilt  = true;

    m_lastBoxSize      = input.boxSizePx;
    m_lastOptions      = options;
    m_lastThreshold    = input.threshold;
    m_lastMonitorScale = input.monitorScale;
    m_lastRectCount    = rectCount;
    if (rectCount > 0)
        std::memcpy(m_lastRects.data(), input.regionRects, sizeof(GlassRenderer::SRegionRect) * rectCount);

    // Restore the caller's state without querying (Hyprland's state at every element boundary)
    g_pHyprRenderer->blend(true);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, 0);
    glBindFramebuffer(GL_FRAMEBUFFER, callerFramebufferId(callerFramebuffer));
    glBindVertexArray(0);
    g_pHyprOpenGL->setViewport(0, 0, static_cast<int>(callerFramebuffer->m_size.x), static_cast<int>(callerFramebuffer->m_size.y));
    return true;
}

int64_t CSilhouetteField::mismatches(const CSilhouetteField& a, const CSilhouetteField& b, const CRegion& area, SP<Render::IFramebuffer> callerFramebuffer) {
    if (!a.m_valid || !b.m_valid || a.m_width != b.m_width || a.m_height != b.m_height || !callerFramebuffer)
        return -1;
    const int w = a.m_width, h = a.m_height;
    std::vector<float> pa(size_t(w) * h * 4), pb(size_t(w) * h * 4);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, a.m_framebuffers[RESULT]);
    glReadPixels(0, 0, w, h, GL_RGBA, GL_FLOAT, pa.data());
    glBindFramebuffer(GL_READ_FRAMEBUFFER, b.m_framebuffers[RESULT]);
    glReadPixels(0, 0, w, h, GL_RGBA, GL_FLOAT, pb.data());
    glBindFramebuffer(GL_FRAMEBUFFER, callerFramebufferId(callerFramebuffer));
    int64_t bad = 0;
    for (const auto& r : area.copy().getRects())
        for (int y = std::max(0, r.y1); y < std::min(h, r.y2); y++)
            for (int x = std::max(0, r.x1); x < std::min(w, r.x2); x++)
                if (pa[(size_t(y) * w + x) * 4] != pb[(size_t(y) * w + x) * 4])
                    bad++;
    return bad;
}
