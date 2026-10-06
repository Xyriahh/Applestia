#include "Diagnostics.hpp"
#include "Globals.hpp"
#include "GlassDecoration.hpp"
#include "GlassSubsurfaceState.hpp"
#include "ItemHints.hpp"
#include "GlassShapes.hpp"
#include "GlassShapeLens.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <format>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>
#include <unordered_set>

#include <EGL/egl.h>
#include <GLES3/gl32.h>
#include <GLES2/gl2ext.h>

#include <hyprland/src/desktop/view/Window.hpp>
#include <hyprland/src/desktop/view/LayerSurface.hpp>
#include <hyprland/src/protocols/core/Compositor.hpp>
#include <hyprland/src/helpers/MiscFunctions.hpp>
#include <hyprland/src/render/Renderer.hpp>
#include <hyprland/src/state/MonitorState.hpp>

namespace Diagnostics {

// BEGIN APPLIED_SHAPES_FORMATTER (also compiled verbatim by the offline unit test)
namespace {
std::string shapeDiagnosticString(std::string_view text) {
    std::string result = "\"";
    constexpr char HEX[] = "0123456789abcdef";
    const size_t limit = std::min(text.size(), MAX_SHAPE_DIAGNOSTIC_STRING);
    size_t consumed = 0;
    for (size_t i = 0; i < limit; ++i) {
        const unsigned char ch = text[i];
        if (ch >= 0xc2) {
            const size_t length = ch <= 0xdf ? 2 : ch <= 0xef ? 3 : ch <= 0xf4 ? 4 : 0;
            if (length && i + length > limit && i + length <= text.size()) break; // Never split a UTF-8 code point at the cap.
            bool valid = length && i + length <= limit;
            for (size_t byte = 1; valid && byte < length; ++byte)
                valid = (static_cast<unsigned char>(text[i + byte]) & 0xc0) == 0x80;
            if (valid) {
                const unsigned char second = text[i + 1];
                valid = !(ch == 0xe0 && second < 0xa0) && !(ch == 0xed && second >= 0xa0) &&
                        !(ch == 0xf0 && second < 0x90) && !(ch == 0xf4 && second >= 0x90);
            }
            if (valid) { result += text.substr(i, length); i += length - 1; consumed = i + 1; continue; }
        }
        if (ch == '"' || ch == '\\') { result += '\\'; result += char(ch); }
        else if (ch < 0x20 || ch >= 0x7f) {
            // Bound and valid JSON even for arbitrary client namespace bytes.
            result += "\\u00"; result += HEX[ch >> 4]; result += HEX[ch & 15];
        } else result += char(ch);
        consumed = i + 1;
    }
    if (consumed < text.size()) result += "...";
    result += '"';
    return result;
}
std::string shapeDiagnosticNumber(double value) {
    return std::isfinite(value) ? std::format("{:.17g}", value) : "null";
}
template <typename T> std::string shapeDiagnosticArray(const T& values) {
    std::string result = "[";
    bool first = true;
    for (const auto value : values) {
        if (!first) result += ',';
        first = false;
        result += shapeDiagnosticNumber(value);
    }
    return result + ']';
}
}

std::string formatAppliedShapes(const std::vector<SAppliedLayerShapes>& layers, bool protocolActive,
                                size_t totalLayers, bool json) {
    std::vector<const SAppliedLayerShapes*> selected;
    selected.reserve(MAX_SHAPE_DIAGNOSTIC_LAYERS);
    // Prefer bound native layers so ordinary background/border layers cannot
    // displace the useful diagnostic when many unrelated surfaces exist.
    for (const bool bound : {true, false}) {
        for (const auto& layer : layers) {
            if (layer.applied.has_value() != bound) continue;
            if (selected.size() < MAX_SHAPE_DIAGNOSTIC_LAYERS) selected.push_back(&layer);
        }
    }
    const size_t omitted = totalLayers > selected.size() ? totalLayers - selected.size() : 0;
    std::string output = json ? std::format("{{\"schemaVersion\":1,\"dataSource\":\"applied\",\"protocolActive\":{},"
        "\"nativeActiveBasis\":\"protocol active, bound and renderingEligible; not a last-draw verdict\","
        "\"coordinateSpace\":\"surface-local logical; clip is x/y/width/height; layerBoxGlobal is global logical\","
        "\"maxLayers\":{},\"maxShapesPerLayer\":{},\"layersTotal\":{},\"layersOmitted\":{},\"layers\":[",
        protocolActive ? "true" : "false", MAX_SHAPE_DIAGNOSTIC_LAYERS, MAX_SHAPE_DIAGNOSTIC_SHAPES, totalLayers, omitted) :
        std::format("hyprglass shapes (APPLIED; protocolActive={}, layers={}/{}, omitted={})\n"
                    "  nativeActive = protocol active + bound + renderingEligible (not last-draw verdict)\n"
                    "  boxes/radii/clip: surface-local logical; clip = x/y/width/height\n",
                    protocolActive, selected.size(), totalLayers, omitted);
    bool firstLayer = true;
    for (const auto* entry : selected) {
        const auto& layer = *entry;
        const bool bound = layer.applied.has_value();
        const size_t total = bound ? layer.applied->size() : 0;
        const size_t count = std::min(total, MAX_SHAPE_DIAGNOSTIC_SHAPES);
        const bool nativeActive = protocolActive && bound && layer.renderingEligible;
        const char* binding = !bound ? "unbound" : total == 0 ? "bound-empty" : "bound";
        if (json) {
            if (!firstLayer) output += ',';
            output += std::format("{{\"namespace\":{},\"monitor\":{},\"layer\":{},\"surface\":{},\"generation\":{},"
                "\"dataSource\":\"applied\",\"binding\":\"{}\",\"bound\":{},\"mapped\":{},\"renderingEligible\":{},\"nativeActive\":{},"
                "\"layerBoxGlobal\":{},\"monitorPosition\":{},\"monitorScale\":{},\"monitorTransform\":{},"
                "\"shapeCount\":{},\"shapesOmitted\":{},\"shapes\":",
                shapeDiagnosticString(layer.nameSpace), shapeDiagnosticString(layer.monitor), shapeDiagnosticString(layer.layer),
                shapeDiagnosticString(layer.surface), layer.generation, binding, bound ? "true" : "false", layer.mapped ? "true" : "false",
                layer.renderingEligible ? "true" : "false", nativeActive ? "true" : "false", shapeDiagnosticArray(layer.layerBoxGlobal),
                shapeDiagnosticArray(layer.monitorPosition), shapeDiagnosticNumber(layer.monitorScale), layer.monitorTransform, total, total-count);
            output += bound ? "[" : "null";
        } else {
            output += std::format("  namespace={} monitor={} layer={} surface={} generation={} binding={} mapped={} eligible={} nativeActive={}\n"
                                  "    layerBoxGlobal={} monitorPosition={} scale={} transform={} shapes={} omitted={}\n",
                shapeDiagnosticString(layer.nameSpace), shapeDiagnosticString(layer.monitor), shapeDiagnosticString(layer.layer),
                shapeDiagnosticString(layer.surface), layer.generation, binding, layer.mapped, layer.renderingEligible, nativeActive,
                shapeDiagnosticArray(layer.layerBoxGlobal), shapeDiagnosticArray(layer.monitorPosition), shapeDiagnosticNumber(layer.monitorScale),
                layer.monitorTransform, total, total-count);
        }
        firstLayer = false;
        for (size_t index = 0; index < count; ++index) {
            const auto& shape = (*layer.applied)[index];
            const auto physical = GlassShapeLens::geometry(shape.width, shape.height, shape.radii, 1.0f);
            const auto clip = shape.clip ? shapeDiagnosticArray(*shape.clip) : "null";
            if (json) {
                if (index) output += ',';
                output += std::format("{{\"insertionIndex\":{},\"depth\":{},\"x\":{},\"y\":{},\"width\":{},\"height\":{},"
                    "\"radii\":{},\"clip\":{},\"opacity\":{},\"tint\":{},\"tintHex\":\"0x{:08x}\",\"preset\":{},"
                    "\"effectiveBezelLogical\":{},\"maxDisplacementLogical\":{}}}",
                    index, shape.depth, shapeDiagnosticNumber(shape.x), shapeDiagnosticNumber(shape.y), shapeDiagnosticNumber(shape.width),
                    shapeDiagnosticNumber(shape.height), shapeDiagnosticArray(shape.radii), clip, shapeDiagnosticNumber(shape.opacity), shape.tint,
                    shape.tint, shapeDiagnosticString(shape.preset), shapeDiagnosticNumber(physical.bezelPx), shapeDiagnosticNumber(physical.displacementPx));
            } else {
                output += std::format("    insertionIndex={} depth={} box=[{},{},{},{}] radii={} clip={} opacity={} tint=0x{:08x} preset={} bezelLogical={} maxDisplacementLogical={}\n",
                    index, shape.depth, shapeDiagnosticNumber(shape.x), shapeDiagnosticNumber(shape.y), shapeDiagnosticNumber(shape.width),
                    shapeDiagnosticNumber(shape.height), shapeDiagnosticArray(shape.radii), clip, shapeDiagnosticNumber(shape.opacity), shape.tint,
                    shapeDiagnosticString(shape.preset), shapeDiagnosticNumber(physical.bezelPx), shapeDiagnosticNumber(physical.displacementPx));
            }
        }
        if (json) output += bound ? "]}" : "}";
    }
    return output + (json ? "]}\n" : "");
}
// END APPLIED_SHAPES_FORMATTER

namespace {

std::unordered_map<MONITORID, SMonitorCounters> s_counters;

SMonitorCounters& countersFor(MONITORID monitor) {
    return s_counters[monitor];
}

// A few frames of slack per stage so a query that isn't ready yet doesn't
// force us to either block or drop a frame's sample.
constexpr int QUERY_RING_SIZE = 3;

struct SStageQueryRing {
    std::array<GLuint, QUERY_RING_SIZE>    queries{};
    std::array<bool, QUERY_RING_SIZE>      pending{};
    // The monitor being rendered when each pending slot's query was opened
    // (captured in CScopedStageTimer's constructor). A query's result is only
    // ever readable well after glEndQuery, on a later call to drainStage(), by
    // which point g_pHyprRenderer->m_renderData.pMonitor may have moved on to
    // another monitor entirely — the result must carry its own monitor id
    // rather than being attributed to whatever is current at drain time.
    std::array<MONITORID, QUERY_RING_SIZE> monitor{};
    int                                    nextSlot = 0;
};

constexpr size_t STAGE_COUNT = static_cast<size_t>(EStage::Count);

std::array<SStageQueryRing, STAGE_COUNT> s_queryRings;
// Stage nanoseconds, keyed by monitor like SMonitorCounters — a monitor's
// stage cost must not be blended into another monitor's average.
std::unordered_map<MONITORID, std::array<uint64_t, STAGE_COUNT>> s_stageNanoseconds;
std::unordered_map<MONITORID, std::array<uint64_t, STAGE_COUNT>> s_stageCpuNanoseconds;

constexpr std::array<std::string_view, STAGE_COUNT> STAGE_NAMES = {
    "sample_background",
    "blur_background",
    "apply_glass_effect",
    "layer_sample",
    "layer_composite",
    "subsurface_sample",
    "subsurface_composite",
    "shape_panel",
    "shapes",
    "shape_foreground",
    "silhouette_build",
};

// Only one GL_TIME_ELAPSED query may be open across the whole GL context at
// once. A bracket opened while this is true silently no-ops rather than
// nesting, which GL forbids outright.
bool s_queryActive = false;

bool                             s_extensionChecked   = false;
bool                             s_extensionAvailable = false;
PFNGLGETQUERYOBJECTUI64VEXTPROC  s_glGetQueryObjectui64vEXT = nullptr;

bool timerExtensionAvailable() {
    if (s_extensionChecked)
        return s_extensionAvailable;
    s_extensionChecked = true;

    const auto* extensions = reinterpret_cast<const char*>(glGetString(GL_EXTENSIONS));
    if (extensions && std::string_view(extensions).find("GL_EXT_disjoint_timer_query") != std::string_view::npos) {
        s_glGetQueryObjectui64vEXT =
            reinterpret_cast<PFNGLGETQUERYOBJECTUI64VEXTPROC>(eglGetProcAddress("glGetQueryObjectui64vEXT"));
        s_extensionAvailable = s_glGetQueryObjectui64vEXT != nullptr;
    }

    return s_extensionAvailable;
}

// Folds every finished query in a stage's ring into its running total.
// Never blocks: only reads results GL already reports as available.
void drainStage(EStage stage) {
    auto& ring = s_queryRings[static_cast<size_t>(stage)];
    for (int i = 0; i < QUERY_RING_SIZE; i++) {
        if (!ring.pending[i])
            continue;

        GLuint available = GL_FALSE;
        glGetQueryObjectuiv(ring.queries[i], GL_QUERY_RESULT_AVAILABLE, &available);
        if (!available)
            continue;

        GLuint64 elapsedNanoseconds = 0;
        s_glGetQueryObjectui64vEXT(ring.queries[i], GL_QUERY_RESULT, &elapsedNanoseconds);
        s_stageNanoseconds[ring.monitor[i]][static_cast<size_t>(stage)] += elapsedNanoseconds;
        ring.pending[i] = false;
    }
}

void drainAllStages() {
    if (!timerExtensionAvailable())
        return;
    for (size_t i = 0; i < STAGE_COUNT; i++)
        drainStage(static_cast<EStage>(i));
}

bool timersEnabledByConfig() {
    return g_pGlobalState && g_pGlobalState->config.debugTimers && **g_pGlobalState->config.debugTimers;
}

// A monitor with no recorded stage timings yet (no CScopedStageTimer bracket
// has closed for it since the last reset) has no entry in s_stageNanoseconds
// — treat that as all-zero rather than inserting one just to read it.
const std::array<uint64_t, STAGE_COUNT>& stageNanosecondsFor(MONITORID id) {
    static const std::array<uint64_t, STAGE_COUNT> ZERO{};
    const auto it = s_stageNanoseconds.find(id);
    return it != s_stageNanoseconds.end() ? it->second : ZERO;
}

std::string monitorLabel(MONITORID id) {
    for (const auto& monitor : State::monitorState()->monitors()) {
        if (monitor && monitor->m_id == id)
            return monitor->m_name;
    }
    return std::format("monitor {}", id);
}

std::string shapeModeLabel(eItemShapeMode mode) {
    switch (mode) {
        case eItemShapeMode::EXPLICIT: return "explicit";
        case eItemShapeMode::INHERIT_WINDOW: return "inherit";
        default: return "none";
    }
}

// Hint and resolution must come from the same draw, or a hint committed since
// would be labelled with the previous hint's verdict.
std::string presetField(const CGlassSubsurfaceState& state, const std::optional<SItemHints>& liveHints) {
    if (!state.hasDrawnOnce())
        return std::format("{} -> -", liveHints && !liveHints->preset.empty() ? liveHints->preset : "-");

    const auto& hint = state.lastPresetHint();
    if (hint.requested.empty())
        return std::format("- -> {}", state.lastResolvedPreset());
    if (hint.rejected)
        return std::format("{} (unknown) -> {}", hint.requested, state.lastResolvedPreset());
    return hint.requested;
}

std::string jsonBox(double x, double y, double width, double height) {
    return std::format("\"x\": {:.1f}, \"y\": {:.1f}, \"width\": {:.1f}, \"height\": {:.1f}", x, y, width, height);
}

template <typename T>
std::string jsonRadii(const T& radii) {
    return std::format("[{:.1f}, {:.1f}, {:.1f}, {:.1f}]", radii[0], radii[1], radii[2], radii[3]);
}

std::string windowField(const PHLWINDOWREF& windowRef) {
    const auto window = windowRef.lock();
    if (!window)
        return "-";
    return std::format("0x{:x} ({})", reinterpret_cast<uintptr_t>(window.get()), window->m_class.empty() ? "-" : window->m_class);
}

std::string formatItems(eHyprCtlOutputFormat format) {
    struct SLiveItem {
        WP<CWLSurfaceResource>                surface;
        std::shared_ptr<CGlassSubsurfaceState> state;
    };

    std::vector<SLiveItem> live;
    if (g_pGlobalState) {
        for (const auto& [surface, state] : g_pGlobalState->subsurfaceGlass) {
            if (surface.expired() || !state)
                continue;
            live.push_back({surface, state});
        }
    }

    const bool subsurfacesEnabled = g_pGlobalState && g_pGlobalState->config.subsurfacesEnabled && **g_pGlobalState->config.subsurfacesEnabled;
    const bool protocolActive     = ItemHints::active();

    if (format == eHyprCtlOutputFormat::FORMAT_JSON) {
        std::string json = std::format("{{\n  \"subsurfacesEnabled\": {}, \"protocolActive\": {}, \"items\": [\n",
                                        subsurfacesEnabled ? "true" : "false", protocolActive ? "true" : "false");

        bool first = true;
        for (const auto& entry : live) {
            const auto  surface = entry.surface.lock();
            const auto  hints   = surface ? ItemHints::forSurface(surface.get()) : std::nullopt;
            const auto  window  = entry.state->window().lock();
            const auto& state   = *entry.state;
            const bool  drawn   = state.hasDrawnOnce();

            if (!first)
                json += ",\n";
            first = false;

            // Hint preset and verdict are the last draw's (see presetField); before any draw, the live hint.
            const std::string hintPreset = drawn ? state.lastPresetHint().requested : (hints ? hints->preset : "");

            std::string hintShape = "null";
            if (hints && hints->shapeMode == eItemShapeMode::EXPLICIT)
                hintShape = std::format("{{{}, \"radii\": {}}}", jsonBox(hints->x, hints->y, hints->width, hints->height), jsonRadii(hints->radii));

            std::string lastDrawn = "null";
            if (drawn) {
                const auto& box = state.lastGlassBox();
                lastDrawn       = std::format("{{\"monitor\": \"{}\", {}, \"radii\": {}, \"roundingPower\": {:.2f}}}", escapeJSONStrings(state.lastMonitorName()),
                                              jsonBox(box.x, box.y, box.w, box.h), jsonRadii(state.lastRadii()), state.lastRoundingPower());
            }

            json += std::format("    {{\"window\": \"{}\", \"windowClass\": \"{}\", \"shapeMode\": \"{}\", \"hintPreset\": \"{}\", "
                                "\"hintPresetRejected\": {}, \"resolvedPreset\": \"{}\", \"hintShape\": {}, \"lastDrawn\": {}}}",
                                window ? std::format("0x{:x}", reinterpret_cast<uintptr_t>(window.get())) : "", escapeJSONStrings(window ? window->m_class : ""),
                                shapeModeLabel(hints ? hints->shapeMode : eItemShapeMode::NONE), escapeJSONStrings(hintPreset),
                                drawn && state.lastPresetHint().rejected ? "true" : "false", escapeJSONStrings(drawn ? state.lastResolvedPreset() : ""), hintShape,
                                lastDrawn);
        }

        json += "\n  ]\n}\n";
        return json;
    }

    if (live.empty())
        return std::format("hyprglass items: no active subsurface glass items (subsurfaces:enabled={}, hyprglass_item_v1 protocol={})\n",
                            subsurfacesEnabled ? "on" : "off", protocolActive ? "active" : "inactive");

    std::string out = "hyprglass items\n";
    out += std::format("  subsurfaces:enabled: {}   hyprglass_item_v1 protocol: {}\n\n", subsurfacesEnabled ? "on" : "off",
                        protocolActive ? "active" : "inactive");
    out += std::format("  {:<30} {:<10} {:<9} {}\n", "window", "monitor", "shape", "preset (requested -> resolved)");

    for (const auto& entry : live) {
        const auto surface   = entry.surface.lock();
        const auto hints     = surface ? ItemHints::forSurface(surface.get()) : std::nullopt;
        const auto shapeMode = hints ? hints->shapeMode : eItemShapeMode::NONE;

        const std::string monitorLabel = entry.state->hasDrawnOnce() ? entry.state->lastMonitorName() : "-";

        out += std::format("  {:<30} {:<10} {:<9} {}\n", windowField(entry.state->window()), monitorLabel, shapeModeLabel(shapeMode),
                            presetField(*entry.state, hints));

        if (hints && shapeMode == eItemShapeMode::EXPLICIT) {
            out += std::format("      hint rect: {:.1f},{:.1f} {:.1f}x{:.1f}px  radii {:.1f},{:.1f},{:.1f},{:.1f} (logical px)\n", hints->x,
                                hints->y, hints->width, hints->height, hints->radii[0], hints->radii[1], hints->radii[2], hints->radii[3]);
        }

        if (entry.state->hasDrawnOnce()) {
            const auto& box   = entry.state->lastGlassBox();
            const auto& radii = entry.state->lastRadii();
            out += std::format(
                "      last drawn: box {:.1f},{:.1f} {:.1f}x{:.1f}px  radii {:.1f},{:.1f},{:.1f},{:.1f}  roundingPower {:.2f} (physical px)\n",
                box.x, box.y, box.w, box.h, radii[0], radii[1], radii[2], radii[3], entry.state->lastRoundingPower());
        } else {
            out += "      last drawn: -\n";
        }
    }

    return out;
}

std::string formatShapes(eHyprCtlOutputFormat format) {
    std::vector<SAppliedLayerShapes> live;
    live.reserve(MAX_SHAPE_DIAGNOSTIC_LAYERS);
    std::unordered_set<Desktop::View::CLayerSurface*> seen;
    size_t total = 0;
    const bool active = GlassShapes::active();
    const bool layersEnabled = g_pGlobalState && g_pGlobalState->config.layersEnabled && **g_pGlobalState->config.layersEnabled;
    const bool hook = g_pGlobalState && g_pGlobalState->renderLayerHook && g_pGlobalState->renderLayerHook->m_original;
    for (const auto& monitor : State::monitorState()->monitors()) {
        if (!monitor) continue;
        for (const auto& level : monitor->m_layerSurfaceLayers) {
            for (const auto& reference : level) {
                const auto layer = reference.lock();
                if (!layer || !seen.insert(layer.get()).second) continue;
                ++total;
                const auto surface = layer->wlSurface();
                const auto root = surface ? surface->resource() : nullptr;
                // The public API returns the state latched on the root's APPLIED
                // commit. Never read cached/pending GUI requests or renderer temps.
                auto applied = root ? GlassShapes::forSurface(root.get()) : std::nullopt;
                auto slot = live.end();
                if (live.size() >= MAX_SHAPE_DIAGNOSTIC_LAYERS) {
                    if (!applied) continue;
                    slot = std::find_if(live.begin(), live.end(), [](const auto& item) { return !item.applied; });
                    if (slot == live.end()) continue;
                }
                const bool namespaceAllowed = g_pGlobalState && !g_pGlobalState->layerNamespaceExclude.contains(layer->m_namespace) &&
                    (g_pGlobalState->layerNamespaceFilter.empty() || g_pGlobalState->layerNamespaceFilter.contains(layer->m_namespace));
                const auto position = layer->position(Desktop::View::IGeometric::GEOMETRIC_CURRENT);
                const auto size = layer->size(Desktop::View::IGeometric::GEOMETRIC_CURRENT);
                SAppliedLayerShapes entry{
                    .layer = std::format("0x{:x}", reinterpret_cast<uintptr_t>(layer.get())),
                    .nameSpace = layer->m_namespace,
                    .monitor = monitor->m_name,
                    .surface = root ? std::format("0x{:x}", reinterpret_cast<uintptr_t>(root.get())) : "",
                    .layerBoxGlobal = {position.x, position.y, size.x, size.y},
                    .monitorPosition = {monitor->m_position.x, monitor->m_position.y},
                    .monitorScale = monitor->m_scale,
                    .monitorTransform = int(monitor->m_transform),
                    .generation = root ? GlassShapes::generationForSurface(root.get()) : 0,
                    .mapped = layer->m_mapped,
                    .renderingEligible = layersEnabled && hook && namespaceAllowed && layer->m_mapped &&
                        monitor->m_transform == WL_OUTPUT_TRANSFORM_NORMAL && currentDebugMode() != EDebugMode::HINTS_ONLY,
                    .applied = std::move(applied),
                };
                if (slot == live.end()) live.push_back(std::move(entry));
                else *slot = std::move(entry);
            }
        }
    }
    return formatAppliedShapes(live, active, total, format == eHyprCtlOutputFormat::FORMAT_JSON);
}

std::string formatStats(eHyprCtlOutputFormat format) {
    drainAllStages();

    const bool timersOn    = timersEnabledByConfig();
    const bool timersReady = timerExtensionAvailable();

    if (format == eHyprCtlOutputFormat::FORMAT_JSON) {
        std::string json = "{\n  \"monitors\": [\n";
        bool        first = true;
        for (const auto& [id, counters] : s_counters) {
            if (!first)
                json += ",\n";
            first = false;
            json += std::format(
                "    {{\"name\": \"{}\", \"frames\": {}, \"windowGlassDraws\": {}, \"windowOpaqueSkipped\": {}, "
                "\"windowCacheHits\": {}, \"windowCacheMisses\": {}, \"windowDeferredResamples\": {}, \"windowPassDiscarded\": {}, "
                "\"layerGlassDraws\": {}, \"layerCacheHits\": {}, \"layerCacheMisses\": {}, \"layerDeferredResamples\": {}, "
                "\"silhouetteBuilds\": {}, \"silhouetteSkips\": {}, \"shapesDrawn\": {}, "
                "\"subsurfaceGlassDraws\": {}, \"subsurfaceCacheHits\": {}, \"subsurfaceCacheMisses\": {}, \"subsurfaceDeferredResamples\": {}, "
                "\"blurPasses\": {}, "
                "\"sampledMegapixels\": {:.3f}, \"glassMegapixels\": {:.3f}, \"stageTimersAvgMicroseconds\": {{",
                escapeJSONStrings(monitorLabel(id)), counters.frames, counters.windowGlassDraws, counters.windowOpaqueSkipped,
                counters.windowCacheHits, counters.windowCacheMisses, counters.windowDeferredResamples, counters.windowPassDiscarded,
                counters.layerGlassDraws, counters.layerCacheHits, counters.layerCacheMisses, counters.layerDeferredResamples,
                counters.silhouetteBuilds, counters.silhouetteSkips, counters.shapesDrawn,
                counters.subsurfaceGlassDraws, counters.subsurfaceCacheHits, counters.subsurfaceCacheMisses, counters.subsurfaceDeferredResamples,
                counters.blurPasses, counters.sampledMegapixels, counters.glassMegapixels);

            const auto& stageNanoseconds = stageNanosecondsFor(id);
            for (size_t i = 0; i < STAGE_COUNT; i++) {
                if (i > 0)
                    json += ", ";
                const double avgMicroseconds = (timersOn && timersReady && counters.frames > 0) ?
                    static_cast<double>(stageNanoseconds[i]) / static_cast<double>(counters.frames) / 1000.0 :
                    0.0;
                json += std::format("\"{}\": {:.3f}", STAGE_NAMES[i], avgMicroseconds);
            }
            json += "}}";
        }
        json += "\n  ],\n";
        json += std::format("  \"timersEnabled\": {}, \"timersAvailable\": {}\n}}\n", timersOn ? "true" : "false",
                             timersReady ? "true" : "false");
        return json;
    }

    std::string out = "hyprglass stats\n";

    if (!timersOn)
        out += "  stage timers: off (plugin:hyprglass:debug:timers = 0)\n";
    else if (!timersReady)
        out += "  stage timers: unavailable (GL_EXT_disjoint_timer_query not supported by this driver)\n";

    if (s_counters.empty())
        out += "  (no frames recorded yet)\n";

    out += std::format(
        "\n  {:<14} {:>8} {:>10} {:>12} {:>9} {:>9} {:>10} {:>9} {:>12} {:>10} {:>10} {:>11} {:>10} {:>12} {:>11} {:>11} {:>10} {:>12} {:>11}\n",
        "monitor", "frames", "win_draws", "opaque_skip", "win_hit", "win_miss", "win_defer", "win_disc", "layer_draws", "layer_hit",
        "layer_miss", "layer_defer", "sub_draws", "sub_hit", "sub_miss", "sub_defer", "blur_pass", "sampled_mpx", "glass_mpx");

    for (const auto& [id, counters] : s_counters) {
        out += std::format(
            "  {:<14} {:>8} {:>10} {:>12} {:>9} {:>9} {:>10} {:>9} {:>12} {:>10} {:>10} {:>11} {:>10} {:>12} {:>11} {:>11} {:>10} {:>12.2f} {:>11.2f}\n",
            monitorLabel(id), counters.frames, counters.windowGlassDraws, counters.windowOpaqueSkipped, counters.windowCacheHits,
            counters.windowCacheMisses, counters.windowDeferredResamples, counters.windowPassDiscarded, counters.layerGlassDraws,
            counters.layerCacheHits, counters.layerCacheMisses, counters.layerDeferredResamples,
            counters.subsurfaceGlassDraws, counters.subsurfaceCacheHits, counters.subsurfaceCacheMisses, counters.subsurfaceDeferredResamples,
            counters.blurPasses, counters.sampledMegapixels, counters.glassMegapixels);

        if (counters.silhouetteBuilds + counters.silhouetteSkips > 0)
            out += std::format("  {:<14} silhouette: {} builds, {} skips, {:.3f} Mtexels submitted; conditional on GPU: {} skipped, {} rebuilt\n", "",
                               counters.silhouetteBuilds, counters.silhouetteSkips, counters.silhouetteTexels / 1e6, counters.silhouetteGpuSkips,
                               counters.silhouetteGpuRebuilds);
        if (counters.frames > 0 && counters.damageMegapixels > 0)
            out += std::format("  {:<14} damage: {:.3f} mpx/frame\n", "", counters.damageMegapixels / counters.frames);
        if (counters.verifyFieldChecks + counters.verifySampleChecks > 0)
            out += std::format("  {:<14} verify: field {} checks {} mismatched texels, sample {} checks {} mismatched texels\n", "",
                               counters.verifyFieldChecks, counters.verifyFieldMismatches, counters.verifySampleChecks, counters.verifySampleMismatches);
        out += std::format("  {:<14} shapesDrawn: {}\n", "", counters.shapesDrawn);

        if (counters.frames > 0) {
            const double frames = static_cast<double>(counters.frames);
            out += std::format(
                "  {:<14} per frame: {:.2f} win draws, {:.2f} layer draws, {:.2f} sub draws, {:.2f} blur passes, {:.3f} sampled mpx, {:.3f} glass mpx\n",
                "", static_cast<double>(counters.windowGlassDraws) / frames, static_cast<double>(counters.layerGlassDraws) / frames,
                static_cast<double>(counters.subsurfaceGlassDraws) / frames, static_cast<double>(counters.blurPasses) / frames,
                counters.sampledMegapixels / frames, counters.glassMegapixels / frames);

            if (timersOn && timersReady) {
                const auto& stageNanoseconds = stageNanosecondsFor(id);
                out += std::format("  {:<14} stage timers (avg microseconds/frame):", "");
                for (size_t i = 0; i < STAGE_COUNT; i++) {
                    const double avgMicroseconds = static_cast<double>(stageNanoseconds[i]) / frames / 1000.0;
                    out += std::format(" {}={:.2f}", STAGE_NAMES[i], avgMicroseconds);
                }
                out += "\n";
                if (const auto cpu = s_stageCpuNanoseconds.find(id); cpu != s_stageCpuNanoseconds.end()) {
                    out += std::format("  {:<14} stage cpu (avg microseconds/frame):", "");
                    for (size_t i = 0; i < STAGE_COUNT; i++)
                        if (cpu->second[i] > 0)
                            out += std::format(" {}={:.2f}", STAGE_NAMES[i], static_cast<double>(cpu->second[i]) / frames / 1000.0);
                    out += "\n";
                }
            }
        }
    }

    return out;
}

// `hyprctl -j hyprglass status` is a contract for shells and scripts: bump it
// when a field is removed, renamed or changes meaning; adding fields keeps it.
constexpr int STATUS_SCHEMA = 1;

struct SFeatureStatus {
    bool             enabled = false;
    std::string_view reason; // empty when the feature is active

    [[nodiscard]] bool active() const noexcept { return reason.empty(); }
};

std::string formatStatus(eHyprCtlOutputFormat format) {
    // Registered after g_pGlobalState is created and unregistered before it is reset.
    const auto& config = g_pGlobalState->config;

    const bool        shadersFailed  = g_pGlobalState->shaderManager.compileFailed();
    const std::string_view shaders   = shadersFailed ? "failed" : g_pGlobalState->shaderManager.isInitialized() ? "ready" : "pending";
    const bool        protocolActive = ItemHints::active();

    // Same order as the render path: the setting, then the hook, then the shaders.
    auto featureStatus = [&](Hyprlang::INT* const* enabledValue, bool hooked) {
        SFeatureStatus status;
        status.enabled = enabledValue && **enabledValue;
        if (!status.enabled)
            status.reason = "disabled";
        else if (!hooked)
            status.reason = "hook_missing";
        else if (shadersFailed)
            status.reason = "shaders_failed";
        return status;
    };

    auto windows = featureStatus(config.enabled, true);
    // With enabled = 0, windows tagged hyprglass_enabled still get glass.
    if (!windows.enabled &&
        std::ranges::any_of(g_pGlobalState->decorations, [](const auto& decoration) {
            auto* glassDecoration = decoration.get();
            return glassDecoration && glassDecoration->isGlassEnabled();
        }))
        windows.reason = shadersFailed ? "shaders_failed" : "";

    const auto layers      = featureStatus(config.layersEnabled, g_pGlobalState->renderLayerHook != nullptr);
    auto       subsurfaces = featureStatus(config.subsurfacesEnabled, g_pGlobalState->renderPassAddHook != nullptr);
    // Item glass draws only on windows that have glass themselves.
    if (subsurfaces.active() && !windows.active())
        subsurfaces.reason = "window_glass_off";

    const bool active = windows.active() || layers.active() || subsurfaces.active();

    if (format == eHyprCtlOutputFormat::FORMAT_JSON) {
        auto featureJson = [](const SFeatureStatus& status) {
            return std::format("{{\"enabled\": {}, \"active\": {}, \"reason\": {}}}", status.enabled ? "true" : "false", status.active() ? "true" : "false",
                               status.active() ? std::string("null") : std::format("\"{}\"", status.reason));
        };
        return std::format("{{\n  \"schema\": {}, \"version\": \"{}\", \"active\": {}, \"shaders\": \"{}\", \"itemProtocol\": {},\n"
                           "  \"features\": {{\n    \"windows\": {},\n    \"layers\": {},\n    \"subsurfaces\": {}\n  }}\n}}\n",
                           STATUS_SCHEMA, escapeJSONStrings(std::string(PLUGIN_VERSION)), active ? "true" : "false", shaders,
                           protocolActive ? "true" : "false", featureJson(windows), featureJson(layers), featureJson(subsurfaces));
    }

    auto featureText = [](const SFeatureStatus& status) {
        if (status.active())
            return std::string("on");
        if (status.reason == "disabled")
            return std::string("off");
        std::string text{status.reason};
        std::ranges::replace(text, '_', ' ');
        return text;
    };

    std::string out = std::format("hyprglass {}: {}\n", PLUGIN_VERSION, active ? "active" : "inactive");
    out += std::format("  shaders: {}\n", shaders);
    out += std::format("  windows: {}   layers: {}   subsurfaces: {}\n", featureText(windows), featureText(layers), featureText(subsurfaces));
    out += std::format("  hyprglass_item_v1 protocol: {}\n", protocolActive ? "active" : "inactive");
    return out;
}

SP<SHyprCtlCommand> s_command;

} // namespace

void recordFrame(MONITORID monitor) {
    countersFor(monitor).frames++;
}

void recordWindowGlassDraw(MONITORID monitor) {
    countersFor(monitor).windowGlassDraws++;
}

void recordWindowOpaqueSkipped(MONITORID monitor) {
    countersFor(monitor).windowOpaqueSkipped++;
}

void recordWindowCacheHit(MONITORID monitor) {
    countersFor(monitor).windowCacheHits++;
}

void recordWindowCacheMiss(MONITORID monitor) {
    countersFor(monitor).windowCacheMisses++;
}

void recordWindowDeferredResample(MONITORID monitor) {
    countersFor(monitor).windowDeferredResamples++;
}

void recordWindowPassDiscarded(MONITORID monitor) {
    countersFor(monitor).windowPassDiscarded++;
}

void recordLayerGlassDraw(MONITORID monitor) {
    countersFor(monitor).layerGlassDraws++;
}

void recordLayerCacheHit(MONITORID monitor) {
    countersFor(monitor).layerCacheHits++;
}

void recordLayerCacheMiss(MONITORID monitor) {
    countersFor(monitor).layerCacheMisses++;
}

void recordLayerDeferredResample(MONITORID monitor) {
    countersFor(monitor).layerDeferredResamples++;
}

void recordSilhouetteBuild(MONITORID monitor) {
    countersFor(monitor).silhouetteBuilds++;
}

void recordSilhouetteSkip(MONITORID monitor) {
    countersFor(monitor).silhouetteSkips++;
}

void recordSilhouetteTexels(MONITORID monitor, double texels) {
    countersFor(monitor).silhouetteTexels += texels;
}

void recordSilhouetteConditional(MONITORID monitor, bool skipped) {
    auto& c = countersFor(monitor);
    (skipped ? c.silhouetteGpuSkips : c.silhouetteGpuRebuilds)++;
}

void recordVerify(MONITORID monitor, bool field, uint64_t mismatches) {
    auto& c = countersFor(monitor);
    (field ? c.verifyFieldChecks : c.verifySampleChecks)++;
    (field ? c.verifyFieldMismatches : c.verifySampleMismatches) += mismatches;
}

void recordDamagePixels(MONITORID monitor, double pixels) {
    countersFor(monitor).damageMegapixels += pixels / 1e6;
}

void recordShapeDraw(MONITORID monitor) {
    countersFor(monitor).shapesDrawn++;
}

void recordSubsurfaceGlassDraw(MONITORID monitor) {
    countersFor(monitor).subsurfaceGlassDraws++;
}

void recordSubsurfaceCacheHit(MONITORID monitor) {
    countersFor(monitor).subsurfaceCacheHits++;
}

void recordSubsurfaceCacheMiss(MONITORID monitor) {
    countersFor(monitor).subsurfaceCacheMisses++;
}

void recordSubsurfaceDeferredResample(MONITORID monitor) {
    countersFor(monitor).subsurfaceDeferredResamples++;
}

void recordBlurPasses(MONITORID monitor, uint64_t passes) {
    countersFor(monitor).blurPasses += passes;
}

void recordStateDesync(const char* what) {
    static std::chrono::steady_clock::time_point lastNotification{};
    const auto now = std::chrono::steady_clock::now();
    if (now - lastNotification < std::chrono::seconds(2))
        return;
    lastNotification = now;
    HyprlandAPI::addNotification(PHANDLE, std::format("hyprglass: GL state drift, {}", what), CHyprColor{1.0, 0.4, 0.2, 1.0}, 3000);
}

void recordSampledPixels(MONITORID monitor, double pixels) {
    countersFor(monitor).sampledMegapixels += pixels / 1'000'000.0;
}

void recordGlassPixels(MONITORID monitor, double pixels) {
    countersFor(monitor).glassMegapixels += pixels / 1'000'000.0;
}

void resetCounters() {
    s_counters.clear();
    s_stageNanoseconds.clear();
    s_stageCpuNanoseconds.clear();
}

void registerHyprCtlCommand(HANDLE handle) {
    s_command = HyprlandAPI::registerHyprCtlCommand(handle, SHyprCtlCommand{
        .name  = "hyprglass",
        .exact = false,
        .fn    = [](eHyprCtlOutputFormat format, std::string request) -> std::string {
            std::string_view rest{request};
            constexpr std::string_view PREFIX = "hyprglass";
            if (rest.starts_with(PREFIX))
                rest.remove_prefix(PREFIX.size());
            while (!rest.empty() && rest.front() == ' ')
                rest.remove_prefix(1);

            if (rest == "stats")
                return formatStats(format);

            if (rest == "stats reset") {
                resetCounters();
                return format == eHyprCtlOutputFormat::FORMAT_JSON ? "{\"ok\": true}\n" : "hyprglass: counters reset\n";
            }

            if (rest == "items")
                return formatItems(format);

            if (rest == "shapes")
                return formatShapes(format);

            if (rest == "status")
                return formatStatus(format);

            return "hyprglass: usage: hyprctl hyprglass <status|stats [reset]|items|shapes>  (add -j for JSON, e.g. hyprctl -j hyprglass shapes)\n";
        },
    });
}

void unregisterHyprCtlCommand(HANDLE handle) {
    if (!s_command)
        return;
    HyprlandAPI::unregisterHyprCtlCommand(handle, s_command);
    s_command.reset();
}

void shutdown() {
    // No bracket should still be open here; end one defensively rather than deleting an active query.
    if (s_queryActive) {
        glEndQuery(GL_TIME_ELAPSED_EXT);
        s_queryActive = false;
    }

    for (auto& ring : s_queryRings) {
        for (auto& query : ring.queries) {
            if (query != 0)
                glDeleteQueries(1, &query);
        }
        ring.queries.fill(0);
        ring.pending.fill(false);
    }
}

CScopedStageTimer::CScopedStageTimer(EStage stage) : m_stage(stage) {
    if (!timersEnabledByConfig())
        return;
    m_cpu      = true;
    m_cpuStart = std::chrono::steady_clock::now();
    if (!timerExtensionAvailable() || s_queryActive)
        return;

    auto& ring = s_queryRings[static_cast<size_t>(stage)];
    drainStage(stage); // opportunistically free up any slots that just finished

    int slot = -1;
    for (int i = 0; i < QUERY_RING_SIZE; i++) {
        const int candidate = (ring.nextSlot + i) % QUERY_RING_SIZE;
        if (!ring.pending[candidate]) {
            slot = candidate;
            break;
        }
    }
    // Ring still full of unread results: skip this frame's sample for this
    // stage rather than stalling on the GPU to free one up.
    if (slot < 0)
        return;

    if (ring.queries[slot] == 0)
        glGenQueries(1, &ring.queries[slot]);

    // Captured now, not at drain time (see SStageQueryRing::monitor) — this
    // bracket always wraps GL work for whichever monitor is currently being
    // rendered.
    MONITORID monitorId = -1; // -1 mirrors Hyprland's own MONITOR_INVALID
    if (const auto monitor = g_pHyprRenderer->m_renderData.pMonitor.lock())
        monitorId = monitor->m_id;
    ring.monitor[slot] = monitorId;

    glBeginQuery(GL_TIME_ELAPSED_EXT, ring.queries[slot]);
    ring.nextSlot = (slot + 1) % QUERY_RING_SIZE;

    m_slot        = slot;
    m_active      = true;
    s_queryActive = true;
}

CScopedStageTimer::~CScopedStageTimer() {
    if (m_cpu) {
        MONITORID monitorId = -1;
        if (const auto monitor = g_pHyprRenderer->m_renderData.pMonitor.lock())
            monitorId = monitor->m_id;
        s_stageCpuNanoseconds[monitorId][static_cast<size_t>(m_stage)] +=
            std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - m_cpuStart).count();
    }
    if (!m_active)
        return;

    glEndQuery(GL_TIME_ELAPSED_EXT);
    s_queryRings[static_cast<size_t>(m_stage)].pending[m_slot] = true;
    s_queryActive                                              = false;
}

} // namespace Diagnostics
