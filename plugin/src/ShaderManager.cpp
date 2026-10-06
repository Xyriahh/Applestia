#include "ShaderManager.hpp"
#include "Globals.hpp"
#include "Shaders.hpp"

#include <GLES3/gl32.h>
#include <hyprland/src/helpers/Color.hpp>
#include <hyprland/src/plugins/PluginAPI.hpp>
#include <hyprland/src/render/OpenGL.hpp>

std::string CShaderManager::loadShaderSource(const char* fileName) {
    if (SHADERS.contains(fileName))
        return SHADERS.at(fileName);

    const std::string message = std::format("[{}] Failed to load shader: {}", PLUGIN_NAME, fileName);
    HyprlandAPI::addNotification(PHANDLE, message, CHyprColor{1.0, 0.2, 0.2, 1.0}, 5000);
    throw std::runtime_error(message);
}

static void lookupGlassUniforms(GLuint program, SGlassUniforms& u) {

    u.refractionStrength  = glGetUniformLocation(program, "refractionStrength");
    u.chromaticAberration = glGetUniformLocation(program, "chromaticAberration");
    u.fresnelStrength     = glGetUniformLocation(program, "fresnelStrength");
    u.specularStrength    = glGetUniformLocation(program, "specularStrength");
    u.glassOpacity        = glGetUniformLocation(program, "glassOpacity");
    u.edgeThickness       = glGetUniformLocation(program, "edgeThickness");
    u.invBezelWidthPx     = glGetUniformLocation(program, "invBezelWidthPx");
    u.uvPadding           = glGetUniformLocation(program, "uvPadding");
    u.tintColor           = glGetUniformLocation(program, "tintColor");
    u.tintAlpha           = glGetUniformLocation(program, "tintAlpha");
    u.lensDistortion      = glGetUniformLocation(program, "lensDistortion");
    u.lensMaxPx           = glGetUniformLocation(program, "lensMaxPx");
    u.saturation          = glGetUniformLocation(program, "saturation");
    u.vibrancyDarkness    = glGetUniformLocation(program, "vibrancyDarkness");
    u.adaptiveDim         = glGetUniformLocation(program, "adaptiveDim");
    u.adaptiveBoost       = glGetUniformLocation(program, "adaptiveBoost");
    u.refractionFlow      = glGetUniformLocation(program, "refractionFlow");
    u.refractionSpread    = glGetUniformLocation(program, "refractionSpread");
    u.fresnelTint         = glGetUniformLocation(program, "fresnelTint");
    u.bevelStrength       = glGetUniformLocation(program, "bevelStrength");
    u.bevelSize           = glGetUniformLocation(program, "bevelSize");
    u.monitorScale        = glGetUniformLocation(program, "monitorScale");
    u.fresnelColor        = glGetUniformLocation(program, "fresnelColor");
    u.fresnelColorAlpha   = glGetUniformLocation(program, "fresnelColorAlpha");
    u.bevelColor          = glGetUniformLocation(program, "bevelColor");
    u.bevelColorAlpha     = glGetUniformLocation(program, "bevelColorAlpha");
    u.bevelTint           = glGetUniformLocation(program, "bevelTint");
    u.bevelAngle          = glGetUniformLocation(program, "bevelAngle");
    u.bevelShadow         = glGetUniformLocation(program, "bevelShadow");
    u.specularAngle       = glGetUniformLocation(program, "specularAngle");
    u.invFullSize         = glGetUniformLocation(program, "invFullSize");
    u.invRoundingPower    = glGetUniformLocation(program, "invRoundingPower");
    u.radii               = glGetUniformLocation(program, "radii");
    u.maskTex             = glGetUniformLocation(program, "maskTex");
    u.useMask             = glGetUniformLocation(program, "useMask");
    u.panelOnly           = glGetUniformLocation(program, "panelOnly");
    u.maskUVOffset        = glGetUniformLocation(program, "maskUVOffset");
    u.maskUVScale         = glGetUniformLocation(program, "maskUVScale");
    u.maskAlphaThreshold  = glGetUniformLocation(program, "maskAlphaThreshold");
    u.maskMode            = glGetUniformLocation(program, "maskMode");
    u.regionRectCount     = glGetUniformLocation(program, "regionRectCount");
    u.regionRects         = glGetUniformLocation(program, "regionRects[0]");
    if (u.regionRects == -1)
        u.regionRects = glGetUniformLocation(program, "regionRects");
    u.sampleUVOffset      = glGetUniformLocation(program, "sampleUVOffset");
    u.sampleUVScale       = glGetUniformLocation(program, "sampleUVScale");
    u.glassBoxOffsetPx    = glGetUniformLocation(program, "glassBoxOffsetPx");
    u.glassBoxSizePx      = glGetUniformLocation(program, "glassBoxSizePx");
    u.useSilhouette       = glGetUniformLocation(program, "useSilhouette");
    u.sdfTex              = glGetUniformLocation(program, "sdfTex");
    u.sdfPxScale          = glGetUniformLocation(program, "sdfPxScale");
    u.silThreshold        = glGetUniformLocation(program, "silThreshold");
    u.silDebug            = glGetUniformLocation(program, "silDebug");
    u.innerCount          = glGetUniformLocation(program, "innerCount");
    u.innerBoxes          = glGetUniformLocation(program, "innerBoxes[0]");
    u.innerRadii          = glGetUniformLocation(program, "innerRadii[0]");
    u.innerClips          = glGetUniformLocation(program, "innerClips[0]");
    u.innerOpacities      = glGetUniformLocation(program, "innerOpacities[0]");

}

bool CShaderManager::compileGlassShader() {
    if (!glassShader->createProgram(
            g_pHyprOpenGL->m_shaders->TEXVERTSRC,
            loadShaderSource("liquidglass.frag"),
            true
        )) {
        HyprlandAPI::addNotification(PHANDLE,
            std::format("[{}] Failed to compile glass shader", PLUGIN_NAME),
            CHyprColor{1.0, 0.2, 0.2, 1.0}, 5000);
        return false;
    }

    lookupGlassUniforms(glassShader->program(), glassUniforms);
    return true;
}

bool CShaderManager::compileBlurShader() {
    if (!blurShader->createProgram(
            g_pHyprOpenGL->m_shaders->TEXVERTSRC,
            loadShaderSource("gaussianblur.frag"),
            true
        )) {
        HyprlandAPI::addNotification(PHANDLE,
            std::format("[{}] Failed to compile blur shader", PLUGIN_NAME),
            CHyprColor{1.0, 0.2, 0.2, 1.0}, 5000);
        return false;
    }

    const auto program = blurShader->program();

    blurUniforms.direction = glGetUniformLocation(program, "direction");
    blurUniforms.radius    = glGetUniformLocation(program, "blurRadius");

    return true;
}

bool CShaderManager::compileSilhouetteShaders() {
    auto& s = silhouette;
    const auto& vert = g_pHyprOpenGL->m_shaders->TEXVERTSRC;
    try {
        if (!s.mask->createProgram(vert, loadShaderSource("silhouette_mask.frag"), true) ||
            !s.seed->createProgram(vert, loadShaderSource("silhouette_seed.frag"), true) ||
            !s.jump->createProgram(vert, loadShaderSource("silhouette_jump.frag"), true) ||
            !s.distance->createProgram(vert, loadShaderSource("silhouette_distance.frag"), true) ||
            !s.smooth->createProgram(vert, loadShaderSource("silhouette_smooth.frag"), true) ||
            !s.diff->createProgram(g_pHyprOpenGL->m_shaders->TEXVERTSRC320, loadShaderSource("silhouette_diff.frag"), true) ||
            !s.copy->createProgram(vert, loadShaderSource("silhouette_copy.frag"), true))
            throw std::runtime_error("compile");
    } catch (...) {
        HyprlandAPI::addNotification(PHANDLE,
            std::format("[{}] Failed to compile silhouette shaders, silhouette layers use box glass", PLUGIN_NAME),
            CHyprColor{1.0, 0.6, 0.2, 1.0}, 5000);
        return false;
    }

    auto loc = [](const SP<CShader>& shader, const char* name) { return glGetUniformLocation(shader->program(), name); };
    s.maskUVOffset  = loc(s.mask, "maskUVOffset");
    s.maskUVScale   = loc(s.mask, "maskUVScale");
    s.maskBoxSizePx = loc(s.mask, "boxSizePx");
    s.maskThreshold = loc(s.mask, "threshold");
    s.maskRectCount = loc(s.mask, "regionRectCount");
    s.maskRects     = loc(s.mask, "regionRects[0]");
    if (s.maskRects == -1)
        s.maskRects = loc(s.mask, "regionRects");
    s.jumpPx             = loc(s.jump, "jumpPx");
    s.distanceSilhouette = loc(s.distance, "silhouette");
    s.distanceLimitPx    = loc(s.distance, "limitPx");
    s.smoothAxis         = loc(s.smooth, "axis");
    s.smoothPx           = loc(s.smooth, "smoothingPx");
    s.diffPrevious       = loc(s.diff, "previous");
    s.diffTileGroups     = loc(s.diff, "tileGroups");
    s.diffFieldTileGroups = loc(s.diff, "fieldTileGroups");
    s.diffFieldTilesX    = loc(s.diff, "fieldTilesX");
    s.diffGeneration     = loc(s.diff, "generation");
    // Optional: partial rebuilds. The fragment stages are the same sources at 3.20.
    s.rowDist = compileCompute(loadShaderSource("silhouette_rowdist.comp"));
    s.lists   = compileCompute(loadShaderSource("silhouette_lists.comp"));
    if (s.rowDist && s.lists) {
        s.rowDistTiles      = glGetUniformLocation(s.rowDist, "tiles");
        s.rowDistGeneration = glGetUniformLocation(s.rowDist, "generation");
        s.rowDistMax        = glGetUniformLocation(s.rowDist, "maxDist");
        s.listsTiles        = glGetUniformLocation(s.lists, "tiles");
        s.listsMax          = glGetUniformLocation(s.lists, "maxDist");
        s.listsPassCount    = glGetUniformLocation(s.lists, "passCount");
        s.listsThresholds   = glGetUniformLocation(s.lists, "thresholds[0]");
        if (s.listsThresholds == -1)
            s.listsThresholds = glGetUniformLocation(s.lists, "thresholds");
        s.listsStride       = glGetUniformLocation(s.lists, "listStride");
        const auto tileVert = loadShaderSource("silhouette_tiles.vert");
        auto at320 = [&](const char* name) {
            auto source = loadShaderSource(name);
            if (const auto at = source.find("#version 300 es"); at != std::string::npos)
                source.replace(at, 15, "#version 320 es");
            return source;
        };
        try {
            s.partialReady = s.seedT->createProgram(tileVert, at320("silhouette_seed.frag"), true, true) &&
                s.jumpT->createProgram(tileVert, at320("silhouette_jump.frag"), true, true) &&
                s.distanceT->createProgram(tileVert, at320("silhouette_distance.frag"), true, true) &&
                s.smoothT->createProgram(tileVert, at320("silhouette_smooth.frag"), true, true) &&
                s.copyT->createProgram(tileVert, at320("silhouette_copy.frag"), true, true);
        } catch (...) {
            s.partialReady = false;
        }
    }
    // Optional: without them the panel draws its whole quad.
    s.tilesReset    = compileCompute(loadShaderSource("silhouette_tiles_reset.comp"));
    s.tilesClassify = compileCompute(loadShaderSource("silhouette_tiles.comp"));
    if (s.tilesClassify) {
        s.tilesMask  = glGetUniformLocation(s.tilesClassify, "mask");
        s.tilesCount = glGetUniformLocation(s.tilesClassify, "tileCount");
    }
    return true;
}

GLuint CShaderManager::compileCompute(const std::string& source) {
    const GLuint shader = glCreateShader(GL_COMPUTE_SHADER);
    const char*  text   = source.c_str();
    glShaderSource(shader, 1, &text, nullptr);
    glCompileShader(shader);
    GLint ok = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        glDeleteShader(shader);
        return 0;
    }
    const GLuint program = glCreateProgram();
    glAttachShader(program, shader);
    glLinkProgram(program);
    glDeleteShader(shader);
    glGetProgramiv(program, GL_LINK_STATUS, &ok);
    if (!ok) {
        glDeleteProgram(program);
        return 0;
    }
    return program;
}

void CShaderManager::initializeGlassTilesIfNeeded() {
    if (m_glassTilesTried)
        return;
    m_glassTilesTried = true;
    GLint major = 0, minor = 0, vertexBlocks = 0;
    glGetIntegerv(GL_MAJOR_VERSION, &major);
    glGetIntegerv(GL_MINOR_VERSION, &minor);
    glGetIntegerv(GL_MAX_VERTEX_SHADER_STORAGE_BLOCKS, &vertexBlocks);
    if (!(major > 3 || (major == 3 && minor >= 2)) || vertexBlocks < 1)
        return;
    // The 3.00 fragment shader is valid 3.20 source; one program needs one version.
    auto fragment = loadShaderSource("liquidglass.frag");
    if (const auto at = fragment.find("#version 300 es"); at != std::string::npos)
        fragment.replace(at, 15, "#version 320 es");
    try {
        if (!glassTilesShader->createProgram(loadShaderSource("liquidglass_tiles.vert"), fragment, true, true))
            return;
    } catch (...) {
        return;
    }
    lookupGlassUniforms(glassTilesShader->program(), glassTilesUniforms);
    glassTileBoxPx    = glGetUniformLocation(glassTilesShader->program(), "tileBoxPx");
    glassTileSizePx   = glGetUniformLocation(glassTilesShader->program(), "tileSizePx");
    auto foreground = loadShaderSource("shapeforeground.frag");
    if (const auto at = foreground.find("#version 300 es"); at != std::string::npos)
        foreground.replace(at, 15, "#version 320 es");
    try {
        if (!foregroundTilesShader->createProgram(loadShaderSource("liquidglass_tiles.vert"), foreground, true, true))
            return;
    } catch (...) {
        return;
    }
    const auto fg          = foregroundTilesShader->program();
    foregroundTilesOffset  = glGetUniformLocation(fg, "uvOffset");
    foregroundTilesScale   = glGetUniformLocation(fg, "uvScale");
    foregroundTileBoxPx    = glGetUniformLocation(fg, "tileBoxPx");
    foregroundTileSizePx   = glGetUniformLocation(fg, "tileSizePx");
    m_glassTilesReady = glassTileBoxPx != -1 && glassTileSizePx != -1 && foregroundTileBoxPx != -1 && foregroundTileSizePx != -1;
}

void CShaderManager::initializeSilhouetteIfNeeded() {
    if (m_silhouetteTried)
        return;
    m_silhouetteTried = true;
    m_silhouetteReady = compileSilhouetteShaders();
}

void CShaderManager::initializeIfNeeded() {
    if (m_initialized)
        return;

    m_initialized   = compileGlassShader() && compileBlurShader();
    m_compileFailed = !m_initialized;
}

bool CShaderManager::compileShapeShaders() {
    auto& s = shapes;
    const auto& vert = g_pHyprOpenGL->m_shaders->TEXVERTSRC;
    if (!s.lens->createProgram(vert, loadShaderSource("liquidshape.frag"), true) ||
        !s.frost->createProgram(vert, loadShaderSource("shapefrost.frag"), true) ||
        !s.foreground->createProgram(vert, loadShaderSource("shapeforeground.frag"), true))
        return false;
    auto loc = [](const SP<CShader>& shader, const char* name) { return glGetUniformLocation(shader->program(), name); };
    s.size = loc(s.lens, "shapeSize"); s.radii = loc(s.lens, "radii");
    s.sampleOffset = loc(s.lens, "sampleOffset"); s.sampleSize = loc(s.lens, "sampleSize");
    s.storageSize = loc(s.lens, "storageSize"); s.softTex = loc(s.lens, "softTex");
    s.bezel = loc(s.lens, "bezel");
    s.bezelSides = loc(s.lens, "bezelSides");
    s.opacity = loc(s.lens, "opacity"); s.tint = loc(s.lens, "tint");
    s.frostMix = loc(s.lens, "frostMix"); s.specular = loc(s.lens, "specular");
    s.inlineFrost = loc(s.lens, "inlineFrost"); s.inlineRadius = loc(s.lens, "inlineRadius");
    s.lensPhysical = loc(s.lens, "lensPhysical");
    s.debug = loc(s.lens, "debugView");
    s.frostSize = loc(s.frost, "sampleSize"); s.frostStorage = loc(s.frost, "storageSize");
    s.frostRadius = loc(s.frost, "radius");
    s.foregroundOffset = loc(s.foreground, "uvOffset"); s.foregroundScale = loc(s.foreground, "uvScale");
    return true;
}

void CShaderManager::initializeShapesIfNeeded() {
    if (m_shapesTried) return;
    m_shapesTried = true;
    try { m_shapesReady = compileShapeShaders(); } catch (...) { m_shapesReady = false; }
    // The native pre-stage withdraws the global and logs the downgrade once.
    // Keeping a failed capability advertised would leave the client body-less.
}

void CShaderManager::destroy() noexcept {
    glassShader->destroy();
    blurShader->destroy();
    for (auto* shader : {&shapes.lens, &shapes.frost, &shapes.foreground})
        (*shader)->destroy();
    m_shapesReady = m_shapesTried = false;
    for (auto* shader : {&silhouette.mask, &silhouette.seed, &silhouette.jump, &silhouette.distance, &silhouette.smooth, &silhouette.diff,
                         &silhouette.copy})
        (*shader)->destroy();
    for (auto* shader : {&silhouette.seedT, &silhouette.jumpT, &silhouette.distanceT, &silhouette.smoothT, &silhouette.copyT})
        (*shader)->destroy();
    silhouette.partialReady = false;
    for (auto* program : {&silhouette.tilesReset, &silhouette.tilesClassify, &silhouette.rowDist, &silhouette.lists})
        if (*program) {
            glDeleteProgram(*program);
            *program = 0;
        }
    glassTilesShader->destroy();
    foregroundTilesShader->destroy();
    m_glassTilesReady = m_glassTilesTried = false;
    m_silhouetteReady = false;
    m_silhouetteTried = false;
    m_initialized   = false;
    m_compileFailed = false;
}
