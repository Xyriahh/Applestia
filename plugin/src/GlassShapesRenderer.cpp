#include "GlassShapesRenderer.hpp"
#include "SilhouetteField.hpp"
#include "BuiltInPresets.hpp"
#include "Diagnostics.hpp"
#include "Globals.hpp"
#include "GlassLayerSurface.hpp"
#include "GlassShapeLens.hpp"
#include "ShapesCapability.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <numeric>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <hyprland/src/Compositor.hpp>
#include <hyprland/src/render/OpenGL.hpp>
#include <hyprland/src/render/Renderer.hpp>
#include <hyprland/src/debug/log/Logger.hpp>
#include <hyprutils/utils/ScopeGuard.hpp>

namespace {
ShapesCapability::SPolicy s_capability;

void writeCapabilityReason(const char* reason, bool fatal) noexcept {
    // Per-instance diagnostic independent of disabled Hyprland logging. Only a
    // regular file owned by us in the existing private runtime dir is writable;
    // reject symlinks and do not follow/replace any foreign inode.
    const char* runtime = std::getenv("XDG_RUNTIME_DIR");
    if (!runtime || !g_pCompositor) return;
    char path[4096];
    const auto& signature = g_pCompositor->m_instanceSignature;
    if (signature.find('/') != std::string::npos) return;
    const int length = std::snprintf(path, sizeof(path), "%s/applestia-shapes-%s.reason", runtime, signature.c_str());
    if (length < 0 || length >= int(sizeof(path))) return;
    const int fd = open(path, O_WRONLY | O_CREAT | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK, 0600);
    if (fd < 0) return;
    struct stat state{};
    if (fstat(fd, &state) == 0 && S_ISREG(state.st_mode) && state.st_uid == geteuid() && state.st_nlink == 1 &&
        ftruncate(fd, 0) == 0) {
        char text[2048];
        const int size = std::snprintf(text, sizeof(text), "%s: %s\n", fatal ? "fatal" : "eligibility", reason);
        if (size > 0) {
            const size_t count = std::min(size_t(size), sizeof(text) - 1);
            (void)write(fd, text, count);
            (void)fsync(fd);
        }
    }
    close(fd);
}

void damageCapabilityTransition() noexcept {
    for (const auto& [_, layer] : g_pGlobalState->layerSurfaces) {
        try { layer->damageForShapesDowngrade(); } catch (...) {}
    }
}

GLuint fbId(const SP<Render::IFramebuffer>& fb) {
    return dynamic_cast<Render::GL::CGLFramebuffer*>(fb.get())->getFBID();
}
constexpr std::array<float, 9> FULLSCREEN = {2, 0, 0, 0, 2, 0, -1, -1, 1};

// Queries once per native stage, never per control. Keep actual GL state and
// Hyprland's capability/viewport/scissor trackers in agreement on the throwing path.
// Program selection deliberately persists: Hyprland caches it privately and
// provides no cache-aware restore by GLuint. All following renderer operations
// must select their shader with useShader(), which can then safely skip/rebind.
class CStateGuard {
  public:
    CStateGuard() {
        glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &draw); glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &read);
        glGetIntegerv(GL_VIEWPORT, viewport); glGetIntegerv(GL_SCISSOR_BOX, scissor);
        glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &vao);
        glGetIntegerv(GL_ACTIVE_TEXTURE, &active);
        for (int i = 0; i < 2; ++i) { glActiveTexture(GL_TEXTURE0 + i); glGetIntegerv(GL_TEXTURE_BINDING_2D, &textures[i]); }
        glActiveTexture(active);
        blend = glIsEnabled(GL_BLEND); stencil = glIsEnabled(GL_STENCIL_TEST); scissors = glIsEnabled(GL_SCISSOR_TEST);
        glGetIntegerv(GL_BLEND_SRC_RGB, &srcRGB); glGetIntegerv(GL_BLEND_DST_RGB, &dstRGB);
        glGetIntegerv(GL_BLEND_SRC_ALPHA, &srcA); glGetIntegerv(GL_BLEND_DST_ALPHA, &dstA);
    }
    ~CStateGuard() {
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, draw); glBindFramebuffer(GL_READ_FRAMEBUFFER, read);
        g_pHyprOpenGL->setViewport(viewport[0], viewport[1], viewport[2], viewport[3]);
        g_pHyprOpenGL->scissor(scissor[0], scissor[1], scissor[2], scissor[3], false);
        // Restore the actual incoming box even if raw GL work on a throwing
        // path changed it while Hyprland's cache already held this value.
        glScissor(scissor[0], scissor[1], scissor[2], scissor[3]);
        g_pHyprOpenGL->setCapStatus(GL_SCISSOR_TEST, scissors); g_pHyprOpenGL->setCapStatus(GL_STENCIL_TEST, stencil);
        // The tracker may already believe the restored value; assert actual caps.
        if (scissors) glEnable(GL_SCISSOR_TEST); else glDisable(GL_SCISSOR_TEST);
        if (stencil) glEnable(GL_STENCIL_TEST); else glDisable(GL_STENCIL_TEST);
        g_pHyprRenderer->blend(blend);
        if (blend) glEnable(GL_BLEND); else glDisable(GL_BLEND);
        glBlendFuncSeparate(srcRGB, dstRGB, srcA, dstA);
        for (int i = 0; i < 2; ++i) { glActiveTexture(GL_TEXTURE0 + i); glBindTexture(GL_TEXTURE_2D, textures[i]); }
        glActiveTexture(active); glBindVertexArray(vao);
    }
  private:
    GLint draw = 0, read = 0, viewport[4]{}, scissor[4]{}, vao = 0, active = 0, textures[2]{};
    GLint srcRGB = 0, dstRGB = 0, srcA = 0, dstA = 0;
    bool blend = false, stencil = false, scissors = false;
};

void cleanCaps() {
    g_pHyprOpenGL->setCapStatus(GL_SCISSOR_TEST, false); glDisable(GL_SCISSOR_TEST);
    g_pHyprOpenGL->setCapStatus(GL_STENCIL_TEST, false); glDisable(GL_STENCIL_TEST);
}

auto use(const SP<CShader>& shader, const std::array<float, 9>& projection) {
    auto bound = g_pHyprOpenGL->useShader(shader);
    bound->setUniformMatrix3fv(SHADER_PROJ, 1, GL_FALSE, projection);
    bound->setUniformInt(SHADER_TEX, 0);
    glBindVertexArray(bound->getUniformLocation(SHADER_SHADER_VAO));
    return bound;
}

int debugView() {
    // Nested diagnostic only: 1 SDF, 2 normals, 3 bezel, 4 sharp/flat,
    // 5 sharp/fixed lens. The last two disable frost/specular identically.
    static const int view = [] {
        const char* value = std::getenv("HYPRGLASS_SHAPES_DEBUG");
        return value && value[0] >= '1' && value[0] <= '5' && value[1] == '\0' ? value[0] - '0' : 0;
    }();
    return view;
}
}

float CGlassShapesRenderer::bezelLogical(const GlassShapes::SGlassShape& shape) {
    return GlassShapeLens::geometry(shape.width, shape.height, shape.radii, 1.0f).bezelPx;
}

void CGlassShapesRenderer::downgrade(const char* reason) noexcept {
    if (s_capability.fatal) return; // First fatal reason survives later config changes.
    s_capability.fatal = true;
    writeCapabilityReason(reason, true);
    // Only called from render stages, never from a surface commit/destruction
    // callback. exit first removes helper callbacks, sends finished, makes
    // objects inert, removes the global/ready marker, then clears listeners.
    // Wayland queues events; this does not dispatch client requests or unload
    // any plugin code while a renderer/listener stack is executing.
    GlassShapes::exit();
    damageCapabilityTransition();
    try {
        Log::logger->log(Log::WARN, "[hyprglass] Native glass shapes globally disabled: {}. Clients must restore legacy material; no automatic retry until plugin initialization.", reason);
    } catch (...) {} // Best effort when the cause itself is allocation failure.
}

void CGlassShapesRenderer::initializeCapability() noexcept {
    s_capability.started = true;
    if (!s_capability.fatal)
        writeCapabilityReason("PLUGIN_INIT protocol initialized; synchronizing drawer eligibility", false);
    syncCapability();
}

void CGlassShapesRenderer::syncCapability() noexcept {
    if (!s_capability.started || !g_pGlobalState) return;
    try {
        const auto& config = g_pGlobalState->config;
        const bool enabled = config.layersEnabled && **config.layersEnabled;
        const bool hook = g_pGlobalState->renderLayerHook && g_pGlobalState->renderLayerHook->m_original;
        const bool allowed = ShapesCapability::eligible(enabled, hook, g_pGlobalState->layerNamespaceFilter,
                                                       g_pGlobalState->layerNamespaceExclude);
        const bool before = GlassShapes::active();
        const bool wasFatal = s_capability.fatal;
        const bool after = s_capability.sync(allowed, before, [] { return GlassShapes::init(); }, [] { GlassShapes::exit(); });
        if (s_capability.fatal) {
            // init() returned false; render failures already recorded their own reason.
            if (!wasFatal) writeCapabilityReason("protocol initialization failed; no automatic retry", true);
        } else if (before != after) {
            writeCapabilityReason(after ? "eligible drawer renderer; new protocol global active" :
                                   !enabled ? "layers.enabled=false" : !hook ? "layer rendering hook unavailable" :
                                   "applestia-drawers excluded or absent from namespace include set", false);
        }
        if (before != after) damageCapabilityTransition();
    } catch (...) {
        downgrade("exception synchronizing native rendering eligibility");
    }
}

void CGlassShapesRenderer::draw(const std::vector<GlassShapes::SGlassShape>& shapes, PHLMONITOR monitor,
                               const CBox& layerBox, SP<Render::IFramebuffer> target, float alpha, const SResolveContext& context) try {
    if (!GlassShapes::active()) return; // Another layer may have downgraded mid-pass.
    if (shapes.empty() || !monitor || !target || alpha <= 0.0f) return;
    if (monitor->m_transform != WL_OUTPUT_TRANSFORM_NORMAL) {
        downgrade("rotated/flipped output is unsupported");
        return;
    }
    auto& manager = g_pGlobalState->shaderManager;
    manager.initializeShapesIfNeeded();
    if (!manager.shapesReady()) {
        downgrade("native shader compilation failed");
        return;
    }
    Diagnostics::CScopedStageTimer timer(Diagnostics::EStage::Shapes);
    CStateGuard state;
    cleanCaps();
    const float scale = monitor->m_scale;
    const auto& u = manager.shapes;
    const CBox framebuffer{0, 0, target->m_size.x, target->m_size.y};
    std::vector<size_t> order(shapes.size());
    std::iota(order.begin(), order.end(), 0);
    std::stable_sort(order.begin(), order.end(), [&](size_t a, size_t b) { return shapes[a].depth < shapes[b].depth; });
    // Child gaps per shape (logical): contained, deeper glass elements only.
    std::vector<std::array<float, 4>> childGaps(shapes.size(), {1e9f, 1e9f, 1e9f, 1e9f});
    for (size_t i = 0; i < shapes.size(); ++i) {
        const auto& p = shapes[i];
        for (size_t j = 0; j < shapes.size(); ++j) {
            const auto& c = shapes[j];
            if (j == i || c.depth <= p.depth || c.opacity <= 0 || c.width <= 0 || c.height <= 0) continue;
            const float tol = 0.5f;
            if (c.x < p.x - tol || c.y < p.y - tol || c.x + c.width > p.x + p.width + tol || c.y + c.height > p.y + p.height + tol) continue;
            auto& g = childGaps[i];
            g[0] = std::min(g[0], c.y - p.y);
            g[1] = std::min(g[1], (p.x + p.width) - (c.x + c.width));
            g[2] = std::min(g[2], (p.y + p.height) - (c.y + c.height));
            g[3] = std::min(g[3], c.x - p.x);
        }
    }

    for (size_t index : order) {
        const auto& shape = shapes[index];
        if (shape.width <= 0 || shape.height <= 0 || shape.opacity <= 0) continue;
        CBox box{layerBox.x + shape.x * scale, layerBox.y + shape.y * scale, shape.width * scale, shape.height * scale};
        CBox visible = box.intersection(layerBox).intersection(framebuffer);
        if (shape.clip) {
            const auto& c = *shape.clip;
            visible = visible.intersection(CBox{layerBox.x + c[0] * scale, layerBox.y + c[1] * scale, c[2] * scale, c[3] * scale});
        }
        if (visible.w <= 0 || visible.h <= 0) continue;
        const auto lens = GlassShapeLens::geometry(shape.width, shape.height, shape.radii, scale);
        const float bezel = lens.bezelPx;
        SResolveContext material{shape.preset.empty() ? context.presetName : shape.preset, context.isDark, context.config, context.customPresets};
        const float frost = std::clamp(resolvePresetFloat(material, &SPresetValues::blurStrength, &SOverridableConfig::blurStrength), 0.0f, 1.0f);
        const float radius = GlassShapeLens::frostRadiusPx(frost, scale);
        // Small-kernel/low-frost material: evaluate the equivalent four-tap
        // blur in the lens and eliminate a draw + shader/FBO/viewport/blend
        // switches per control. Keep wider/stronger frost on its old pass.
        const bool inlineFrost = frost > 0.0f && frost <= 0.25f && radius <= 1.0f;
        const bool separateFrost = frost > 0.0f && !inlineFrost;
        CBox sample = visible;
        sample.expand(GlassShapeLens::samplePaddingPx(lens, frost, scale));
        sample = sample.intersection(framebuffer);
        // Outward integer bounds: every copied pixel is initialized and the
        // antialias/dispersion/blur footprint is inside this snapshot.
        const int x0 = static_cast<int>(std::floor(sample.x)), y0 = static_cast<int>(std::floor(sample.y));
        const int w = static_cast<int>(std::ceil(sample.x + sample.w)) - x0;
        const int h = static_cast<int>(std::ceil(sample.y + sample.h)) - y0;
        sample = CBox{double(x0), double(y0), double(w), double(h)};
        if (!CRegion(sample).subtract(g_pHyprRenderer->m_renderData.damage).empty()) {
            auto damage = sample; damage.scale(1.0 / scale).translate(monitor->m_position);
            g_pHyprRenderer->damageBox(damage);
            continue; // Never sample undefined pixels after a small commit.
        }
        // Grow by 64px buckets, reuse across controls/frames. Draw only w*h,
        // not the retained capacity; initialized bounds are explicit uniforms.
        for (auto* scratch : {&m_snapshot, &m_frost}) {
            if (scratch == &m_frost && !separateFrost) continue;
            if (!*scratch) *scratch = g_pHyprRenderer->createFB("hyprglass-shape-region");
            if (!*scratch) {
                downgrade("native region framebuffer creation failed");
                return;
            }
            if ((*scratch)->m_size.x < w || (*scratch)->m_size.y < h || (*scratch)->m_drmFormat != target->m_drmFormat) {
                const int width = std::max(w, static_cast<int>((*scratch)->m_size.x));
                const int height = std::max(h, static_cast<int>((*scratch)->m_size.y));
                if (!(*scratch)->alloc(((width + 63) / 64) * 64, ((height + 63) / 64) * 64, target->m_drmFormat)) {
                    downgrade("native region framebuffer allocation failed");
                    return; // GL guard restores state; caller still draws foreground.
                }
            }
        }
        cleanCaps();
        glBindFramebuffer(GL_READ_FRAMEBUFFER, fbId(target));
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, fbId(m_snapshot));
        glBlitFramebuffer(x0, y0, x0 + w, y0 + h, 0, 0, w, h, GL_COLOR_BUFFER_BIT, GL_NEAREST);
        Diagnostics::recordSampledPixels(monitor->m_id, static_cast<double>(w) * h);
        const auto storage = m_snapshot->m_size;
        glActiveTexture(GL_TEXTURE0); m_snapshot->getTexture()->bind();
        if (separateFrost) {
            glBindFramebuffer(GL_FRAMEBUFFER, fbId(m_frost));
            g_pHyprRenderer->blend(false);
            g_pHyprOpenGL->setViewport(0, 0, w, h);
            auto shader = use(u.frost, FULLSCREEN);
            glUniform2f(u.frostSize, w, h); glUniform2f(u.frostStorage, storage.x, storage.y);
            glUniform1f(u.frostRadius, radius);
            glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
            Diagnostics::recordBlurPasses(monitor->m_id, 1);
        }
        glBindFramebuffer(GL_FRAMEBUFFER, fbId(target));
        g_pHyprOpenGL->setViewport(0, 0, target->m_size.x, target->m_size.y);
        g_pHyprRenderer->blend(true); glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
        glActiveTexture(GL_TEXTURE0); m_snapshot->getTexture()->bind();
        glActiveTexture(GL_TEXTURE1); (separateFrost ? m_frost : m_snapshot)->getTexture()->bind();
        glActiveTexture(GL_TEXTURE0);
        auto matrix = g_pHyprRenderer->projectBoxToTarget(box); matrix.transpose();
        auto shader = use(u.lens, matrix.getMatrix());
        glUniform1i(u.softTex, 1);
        glUniform2f(u.size, box.w, box.h);
        glUniform2f(u.sampleOffset, box.x - x0, box.y - y0);
        glUniform2f(u.sampleSize, w, h); glUniform2f(u.storageSize, storage.x, storage.y);
        const float half = 0.5f * std::min(shape.width, shape.height);
        glUniform4f(u.radii, std::clamp(shape.radii[0], 0.0f, half) * scale, std::clamp(shape.radii[1], 0.0f, half) * scale,
                            std::clamp(shape.radii[2], 0.0f, half) * scale, std::clamp(shape.radii[3], 0.0f, half) * scale);
        glUniform1f(u.bezel, bezel);
        const auto sides = GlassShapeLens::sideBezels(bezel, childGaps[index], scale);
        glUniform4f(u.bezelSides, sides[0], sides[1], sides[2], sides[3]);
        // x = displacement per px of band (shared IOR); the shader scales it by the per-side band
        glUniform3f(u.lensPhysical, bezel > 0.0f ? lens.displacementPx / bezel : 0.0f, lens.chromaticPx, lens.lipPx);
        glUniform1f(u.opacity, std::clamp(shape.opacity * alpha, 0.0f, 1.0f));
        const auto presetTint = static_cast<uint32_t>(resolvePresetInt(material, &SPresetValues::tintColor, &SOverridableConfig::tintColor));
        const float shapeA = (shape.tint & 255) / 255.0f, presetA = (presetTint & 255) / 255.0f;
        const float tintA = shapeA + presetA * (1.0f - shapeA);
        auto tintChannel = [&](int shift) {
            const float s = ((shape.tint >> shift) & 255) / 255.0f;
            const float p = ((presetTint >> shift) & 255) / 255.0f;
            return tintA > 0 ? (s * shapeA + p * presetA * (1.0f - shapeA)) / tintA : 0.0f;
        };
        glUniform4f(u.tint, tintChannel(24), tintChannel(16), tintChannel(8), tintA);
        glUniform1f(u.frostMix, frost);
        glUniform1i(u.inlineFrost, inlineFrost ? 1 : 0);
        glUniform1f(u.inlineRadius, radius);
        glUniform1f(u.specular, std::clamp(resolvePresetFloat(material, &SPresetValues::specularStrength, &SOverridableConfig::specularStrength), 0.0f, 1.0f));
        glUniform1i(u.debug, debugView());
        // Clip is a flat cut in original surface coordinates, never a new SDF.
        g_pHyprOpenGL->scissor(visible, false);
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
        g_pHyprOpenGL->scissor(nullptr);
        Diagnostics::recordShapeDraw(monitor->m_id);
        Diagnostics::recordGlassPixels(monitor->m_id, visible.w * visible.h);
    }
} catch (...) {
    // All local GL/FBO guards have unwound before withdrawal. This catches
    // throwing allocations too; a nonthrowing false result is handled above.
    downgrade("exception preparing/drawing native regions");
}

void CGlassShapesRenderer::foreground(SP<Render::IFramebuffer> surface, SP<Render::IFramebuffer> target,
                                     const CBox& rawBox, const CBox& transformedBox, GLuint tileBuffer) {
    if (!surface || !target) return;
    Diagnostics::CScopedStageTimer timer(Diagnostics::EStage::ShapeForeground);
    CStateGuard state;
    cleanCaps();
    auto& manager = g_pGlobalState->shaderManager;
    manager.initializeShapesIfNeeded();
    // A successful pre-stage guarantees this program. Withdrawal intentionally
    // does NOT destroy shaders: captured foreground is still drawn when active
    // is false. Defensively withdraw if that invariant is ever broken.
    if (!manager.shapesReady()) {
        downgrade("native foreground shader unexpectedly unavailable");
        return;
    }
    glBindFramebuffer(GL_FRAMEBUFFER, fbId(target));
    g_pHyprOpenGL->setViewport(0, 0, target->m_size.x, target->m_size.y);
    g_pHyprRenderer->blend(true); glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    glActiveTexture(GL_TEXTURE0); surface->getTexture()->bind();
    const auto monitor = g_pHyprRenderer->m_renderData.pMonitor.lock();
    const auto transform = Math::wlTransformToHyprutils(Math::invertTransform(monitor->m_transform));
    auto matrix = g_pHyprRenderer->projectBoxToTarget(rawBox, transform); matrix.transpose();
    if (tileBuffer)
        manager.initializeGlassTilesIfNeeded();
    const bool tiles = tileBuffer && manager.glassTilesReady();
    auto shader = use(tiles ? manager.foregroundTilesShader : manager.shapes.foreground, matrix.getMatrix());
    glUniform2f(tiles ? manager.foregroundTilesOffset : manager.shapes.foregroundOffset, transformedBox.x / surface->m_size.x, transformedBox.y / surface->m_size.y);
    glUniform2f(tiles ? manager.foregroundTilesScale : manager.shapes.foregroundScale, transformedBox.w / surface->m_size.x, transformedBox.h / surface->m_size.y);
    if (tiles) {
        // Only tiles holding a non-zero pixel: (0,0,0,0) adds nothing under GL_ONE, GL_ONE_MINUS_SRC_ALPHA.
        glUniform2f(manager.foregroundTileBoxPx, static_cast<float>(transformedBox.w), static_cast<float>(transformedBox.h));
        glUniform1f(manager.foregroundTileSizePx, CSilhouetteField::TILE_SIZE_PX);
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, tileBuffer);
        glBindBuffer(GL_DRAW_INDIRECT_BUFFER, tileBuffer);
    }
    auto drawQuad = [&] {
        if (tiles)
            glDrawArraysIndirect(GL_TRIANGLE_STRIP, nullptr);
        else
            glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    };
    const Hyprutils::Utils::CScopeGuard unbindTiles([&] {
        if (tiles) {
            glBindBuffer(GL_DRAW_INDIRECT_BUFFER, 0);
            glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, 0);
        }
    });
    // Per damage rect: two small changes in opposite corners must not copy the
    // whole layer between them. Many rects fall back to their extents.
    auto& damage = g_pHyprRenderer->m_renderData.damage;
    const auto rects = damage.getRects();
    if (rects.size() > 32) {
        const auto extents = damage.getExtents().intersection(transformedBox);
        if (extents.w > 0 && extents.h > 0) {
            g_pHyprOpenGL->scissor(extents, false);
            drawQuad();
        }
        return;
    }
    for (const auto& r : rects) {
        const auto box = CBox{double(r.x1), double(r.y1), double(r.x2 - r.x1), double(r.y2 - r.y1)}.intersection(transformedBox);
        if (box.w <= 0 || box.h <= 0) continue;
        g_pHyprOpenGL->scissor(box, false);
        drawQuad();
    }
}
