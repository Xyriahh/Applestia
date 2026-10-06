#pragma once

#include <GLES3/gl32.h>
#include <hyprland/src/render/Shader.hpp>
#include <string>

struct SGlassUniforms {
    GLint refractionStrength = -1;
    GLint chromaticAberration = -1;
    GLint fresnelStrength = -1;
    GLint specularStrength = -1;
    GLint glassOpacity = -1;
    GLint edgeThickness = -1;
    GLint invBezelWidthPx = -1;
    GLint uvPadding = -1;
    GLint tintColor = -1;
    GLint tintAlpha = -1;
    GLint lensDistortion = -1;
    GLint lensMaxPx = -1;
    GLint saturation = -1;
    GLint vibrancyDarkness = -1;
    GLint adaptiveDim = -1;
    GLint adaptiveBoost = -1;
    GLint refractionFlow = -1;
    GLint refractionSpread = -1;
    GLint fresnelTint = -1;
    GLint bevelStrength = -1;
    GLint bevelSize = -1;
    GLint monitorScale = -1;
    GLint fresnelColor = -1;
    GLint fresnelColorAlpha = -1;
    GLint bevelColor = -1;
    GLint bevelColorAlpha = -1;
    GLint bevelTint = -1;
    GLint bevelAngle = -1;
    GLint bevelShadow = -1;
    GLint specularAngle = -1;
    GLint invFullSize = -1;
    GLint invRoundingPower = -1;
    GLint radii = -1; // per-corner radius: top-left, top-right, bottom-right, bottom-left

    // Layers only: temp FBO surface mask for content-aware glass
    GLint maskTex = -1;
    GLint useMask = -1;
    GLint panelOnly = -1;
    GLint maskUVOffset = -1;
    GLint maskUVScale = -1;
    GLint maskAlphaThreshold = -1;
    GLint maskMode = -1;
    GLint regionRectCount = -1;
    GLint regionRects = -1;
    GLint sampleUVOffset = -1;
    GLint sampleUVScale = -1;

    // Subsurface item glass only: glass-box sub-rect the SDF is measured
    // against, in box-local pixels (see Shaders.hpp).
    GLint glassBoxOffsetPx = -1;
    GLint glassBoxSizePx = -1;

    // Silhouette shape mode only (layers): distance field on texture unit 2.
    GLint useSilhouette = -1;
    GLint sdfTex = -1;
    GLint sdfPxScale = -1;   // framebuffer px per field px
    GLint silThreshold = -1; // alpha of a fully covered pixel
    GLint silDebug = -1;
    GLint innerCount = -1, innerBoxes = -1, innerRadii = -1, innerClips = -1, innerOpacities = -1;
};

// Silhouette shape mode: distance field passes (see SilhouetteField.cpp).
struct SSilhouetteShaders {
    SP<CShader> mask     = makeShared<CShader>();
    SP<CShader> seed     = makeShared<CShader>();
    SP<CShader> jump     = makeShared<CShader>();
    SP<CShader> distance = makeShared<CShader>();
    SP<CShader> smooth   = makeShared<CShader>();
    SP<CShader> diff     = makeShared<CShader>();
    SP<CShader> copy     = makeShared<CShader>();

    GLint diffPrevious = -1, diffTileGroups = -1, diffFieldTileGroups = -1, diffFieldTilesX = -1, diffGeneration = -1;
    // Partial rebuilds (GLES 3.2): change-area computes and tile-listed field passes
    GLuint rowDist = 0, lists = 0;
    GLint  rowDistTiles = -1, rowDistGeneration = -1, rowDistMax = -1;
    GLint  listsTiles = -1, listsMax = -1, listsPassCount = -1, listsThresholds = -1, listsStride = -1;
    SP<CShader> seedT = makeShared<CShader>(), jumpT = makeShared<CShader>(), distanceT = makeShared<CShader>(),
                smoothT = makeShared<CShader>(), copyT = makeShared<CShader>();
    bool partialReady = false;
    // Panel tile list (compute, GLES 3.1+): 0 when unavailable
    GLuint tilesReset = 0, tilesClassify = 0;
    GLint  tilesMask = -1, tilesCount = -1;
    GLint maskUVOffset = -1, maskUVScale = -1, maskBoxSizePx = -1, maskThreshold = -1, maskRectCount = -1, maskRects = -1;
    GLint jumpPx = -1;
    GLint distanceSilhouette = -1, distanceLimitPx = -1;
    GLint smoothAxis = -1, smoothPx = -1;
};

struct SBlurUniforms {
    GLint direction = -1;
    GLint radius    = -1;
};

struct SShapeShaders {
    SP<CShader> lens = makeShared<CShader>();
    SP<CShader> frost = makeShared<CShader>();
    SP<CShader> foreground = makeShared<CShader>();
    GLint size = -1, radii = -1, sampleOffset = -1, sampleSize = -1, storageSize = -1;
    GLint softTex = -1, bezel = -1, bezelSides = -1, opacity = -1, tint = -1;
    GLint frostMix = -1, specular = -1, debug = -1;
    GLint inlineFrost = -1, inlineRadius = -1;
    GLint lensPhysical = -1;
    GLint frostSize = -1, frostStorage = -1, frostRadius = -1;
    GLint foregroundOffset = -1, foregroundScale = -1;
};

class CShaderManager {
  public:
    [[nodiscard]] bool isInitialized() const noexcept { return m_initialized; }
    // The last compile attempt failed; it is retried at the next glass draw.
    [[nodiscard]] bool compileFailed() const noexcept { return m_compileFailed; }

    void initializeIfNeeded();
    void destroy() noexcept;

    SP<CShader>    glassShader = makeShared<CShader>();
    SGlassUniforms glassUniforms;

    // Panel glass drawn over a GPU-built tile list (same fragment shader).
    [[nodiscard]] bool glassTilesReady() const noexcept { return m_glassTilesReady; }
    void               initializeGlassTilesIfNeeded();
    SP<CShader>    glassTilesShader = makeShared<CShader>();
    SGlassUniforms glassTilesUniforms;
    GLint          glassTileBoxPx = -1, glassTileSizePx = -1;
    SP<CShader>    foregroundTilesShader = makeShared<CShader>();
    GLint          foregroundTilesOffset = -1, foregroundTilesScale = -1, foregroundTileBoxPx = -1, foregroundTileSizePx = -1;

    SP<CShader>    blurShader = makeShared<CShader>();
    SBlurUniforms  blurUniforms;

    // Compiled on first use by a silhouette layer; a failure only disables that
    // mode (layers fall back to box glass), never the plugin.
    [[nodiscard]] bool silhouetteReady() const noexcept { return m_silhouetteReady; }
    void               initializeSilhouetteIfNeeded();
    SSilhouetteShaders silhouette;
    void initializeShapesIfNeeded();
    [[nodiscard]] bool shapesReady() const noexcept { return m_shapesReady; }
    SShapeShaders shapes;

  private:
    bool m_initialized = false;
    bool m_compileFailed = false;
    bool m_silhouetteReady = false;
    bool m_silhouetteTried = false;
    bool m_shapesReady = false, m_shapesTried = false;
    bool m_glassTilesReady = false, m_glassTilesTried = false;

    [[nodiscard]] static std::string loadShaderSource(const char* fileName);
    [[nodiscard]] bool compileGlassShader();
    [[nodiscard]] bool compileBlurShader();
    [[nodiscard]] bool compileSilhouetteShaders();
    [[nodiscard]] bool compileShapeShaders();
    [[nodiscard]] static GLuint compileCompute(const std::string& source);
};
