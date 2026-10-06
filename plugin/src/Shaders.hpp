#pragma once

#include <unordered_map>
#include <string>

inline const std::unordered_map<std::string, const char*> SHADERS = {
    {"liquidglass.frag", R"GLSL(
#version 300 es
precision highp float;

/*
 * Apple-style Liquid Glass Fragment Shader — Thick-glass refraction model
 *
 * The window is modeled as a thick convex glass slab:
 *   - Center: flat surface → clean frosted blur, no distortion
 *   - Edges: curved surface → refraction pulls in content from beyond
 *     the window boundary, creating natural color bleeding
 *
 * Rendering layers:
 * 1. Edge refraction via smooth outward direction (optionally along the edges,
 *    optionally rim-only) + exponential proximity
 * 2. Chromatic aberration (per-channel refraction scale)
 * 3. Edge raw-texture blend for vivid color pickup
 * 4. Subtle center dome lens magnification
 * 5. Frosted tint (brightness boost + desaturation)
 * 6. Configurable color tint overlay
 * 7. Bevel (thin lit line at the edge)
 * 8. Fresnel edge glow (white or tinted by the background)
 * 9. Specular highlight (top)
 * 10. Inner shadow (bottom rim)
 */

uniform sampler2D tex;
uniform vec2 fullSize;
uniform vec2 invFullSize;      // = 1.0 / fullSize, hoisted out of the per-pixel divisions below
uniform vec4 radii;            // per-corner radius: top-left, top-right, bottom-right, bottom-left
uniform vec2 uvPadding;

uniform float refractionStrength;
uniform float chromaticAberration;
uniform float fresnelStrength;
uniform float specularStrength;
uniform float glassOpacity;
uniform float edgeThickness;
uniform float invBezelWidthPx; // = 1.0 / (edgeThickness * minDim), hoisted per-draw
uniform vec3 tintColor;
uniform float tintAlpha;
uniform float lensDistortion;
uniform float lensMaxPx;       // = lensDistortion * minDim * 0.006, hoisted per-draw
uniform float brightness;
uniform float contrast;
uniform float saturation;
uniform float vibrancy;
uniform float vibrancyDarkness;
uniform float adaptiveDim;
uniform float adaptiveBoost;
uniform float roundingPower;
uniform float invRoundingPower; // = 1.0 / roundingPower, hoisted per-draw
uniform float refractionFlow;
uniform float refractionSpread;
uniform float fresnelTint;
uniform float bevelStrength;
uniform float bevelSize;
uniform float monitorScale;
uniform vec3 fresnelColor;
uniform float fresnelColorAlpha;
uniform vec3 bevelColor;
uniform float bevelColorAlpha;
uniform float bevelTint;
uniform float bevelAngle;
uniform float bevelShadow;
uniform float specularAngle;

uniform sampler2D maskTex;
uniform int useMask;
uniform int panelOnly; // native path: foreground is drawn separately, exactly once
uniform vec2 maskUVOffset;
uniform vec2 maskUVScale;
uniform float maskAlphaThreshold;
uniform int maskMode;          // 0 = alpha threshold, 1 = protocol region
uniform int regionRectCount;   // 0..16
uniform vec4 regionRects[16];  // box-local pixels: xy = offset from box top-left, zw = size

// Subsurface item glass only: the rounded-box SDF (getCornerSDF below) is
// evaluated over this sub-rect of the drawn box instead of the full box —
// the item's blur-region extents, which can be smaller than its own surface
// box (e.g. a capsule pill inside a wider hit-test area). Box-local pixels,
// same space as regionRects. Windows and alpha-mask layers pass offset (0,0)
// and size == fullSize, so the SDF is unchanged from the old whole-box math.
uniform vec2 glassBoxOffsetPx;
uniform vec2 glassBoxSizePx;

// Maps this fragment's own box UV into the sample texture's normalized space
// before uvPadding is applied. Identity (offset 0, scale 1) unless the sample
// texture covers a smaller area than this box — PROTOCOL_REGION layers only,
// where the background is only ever sampled/blurred inside the blur region's
// bounding box (see GlassRenderer::sampleBackground callers in GlassLayerSurface.cpp).
uniform vec2 sampleUVOffset;
uniform vec2 sampleUVScale;

// Silhouette shape mode (layers only): shape and normals come from a signed
// distance field of the layer's own alpha (SilhouetteField.cpp) instead of one
// rounded box spanning the layer. The field covers exactly this quad, so it is
// sampled at the quad UV. Values are field px, positive inside.
uniform int useSilhouette;
uniform int innerCount;
uniform vec4 innerBoxes[64];
uniform vec4 innerRadii[64];
uniform vec4 innerClips[64];
uniform float innerOpacities[64];
uniform sampler2D sdfTex;
uniform float sdfPxScale;     // framebuffer px per field px
uniform float silThreshold;   // surface alpha of a fully covered pixel
uniform int silDebug;         // 1 = coverage, 2 = distance, 3 = normals

in vec2 v_texcoord;
layout(location = 0) out vec4 fragColor;

// ============================================================================
// TEXTURE SAMPLING (window UV -> padded texture UV)
// ============================================================================

// Box UV -> sample-texture-local UV (undoes sampleUVOffset/uvScale's
// shrink before the padding remap below sees it).
vec2 toSampleBoxUV(vec2 wuv) {
    return (wuv - sampleUVOffset) / sampleUVScale;
}

vec2 toTexUV(vec2 wuv) {
    return wuv * (1.0 - 2.0 * uvPadding) + uvPadding;
}

vec4 sampleBlurred(vec2 wuv) {
    vec2 tuv = toTexUV(toSampleBoxUV(wuv));
    return texture(tex, clamp(tuv, 0.001, 0.999));
}

// ============================================================================
// SDF
// ============================================================================

float lpNorm(vec2 v, float p, float invP) {
    // Exact identity: pow(x^2+y^2, 0.5) == length(v) when p == 2.0 (the
    // Hyprland default). Native sqrt is a single correctly-rounded hardware
    // op vs. two pow()s (exp2/log2-based) + a third pow() for the outer root.
    if (p == 2.0) return length(v);
    return pow(pow(abs(v.x), p) + pow(abs(v.y), p), invP);
}

// Quadrant lookup for the per-corner radius. p is measured from the box
// center (as getRoundedBoxSDF/getBevelSDF compute it below): negative y is
// the top half (v_texcoord grows downward — see the specular highlight's
// 1.0 - uv.y), negative x is the left half. cornerRadii is (top-left,
// top-right, bottom-right, bottom-left).
float pickCornerRadius(vec2 p, vec4 cornerRadii) {
    float top    = p.x < 0.0 ? cornerRadii.x : cornerRadii.y;
    float bottom = p.x < 0.0 ? cornerRadii.w : cornerRadii.z;
    return p.y < 0.0 ? top : bottom;
}

// posPx/boxSizePx: pixel-space position relative to (and size of) the box the
// SDF is measured against — the glass box (see glassBoxOffsetPx/SizePx above),
// not necessarily the fragment's full drawn box.
float getRoundedBoxSDF(vec2 posPx, vec2 boxSizePx, vec4 cornerRadii) {
    vec2 p = posPx - boxSizePx * 0.5;
    vec2 halfSize = boxSizePx * 0.5;
    float r = pickCornerRadius(p, cornerRadii);
    float clampedR = min(r, min(halfSize.x, halfSize.y));
    vec2 q = abs(p) - halfSize + clampedR;
    return min(max(q.x, q.y), 0.0) + lpNorm(max(q, 0.0), roundingPower, invRoundingPower) - clampedR;
}

float getCornerSDF(vec2 uv) {
    vec2 boxLocalPx = uv * fullSize - glassBoxOffsetPx;
    return getRoundedBoxSDF(boxLocalPx, glassBoxSizePx, radii);
}

// Edge distance for the bevel, crease-free. Inside the window, getCornerSDF is
// max(q.x, q.y) - r: the distance to whichever edge is nearer. That has a crease
// along each corner's 45-degree diagonal, so the edge refraction (and anything
// else driven by edgeProximity) shows a straight seam in all four corners.
//
// Here the contour lines are rounded rectangles that match the window outline
// exactly at the edge (depth 0) and get rounder going inward: at depth d the box
// is inset by d and its corner radius is r + d. The radius only grows, so the
// top and side bevels always blend round the corner. Per pixel the depth is a
// closed-form quadratic in the corner zone:
//   |a + 2d| = r + d,  a = |p| - halfSize + r   =>   7d^2 + (4(ax+ay) - 2r) d + |a|^2 - r^2 = 0
// and the plain straight-edge distance elsewhere. Past depth (h - r) / 2, with h
// the smaller half-size, the radius would outgrow the inset box: from there the
// contours are stadiums, i.e. the plain distance to the box rounded by h (so a
// capsule gets its exact distance everywhere).
float getBevelSDF(vec2 uv) {
    vec2  H = glassBoxSizePx * 0.5;
    float h = min(H.x, H.y);
    vec2  signedP = uv * fullSize - glassBoxOffsetPx - H;
    float r = min(pickCornerRadius(signedP, radii), h);
    vec2  p = abs(signedP);
    vec2  a = p - H + r;
    float d = min(r - a.x, r - a.y);                      // straight-edge depth
    float B = 4.0 * (a.x + a.y) - 2.0 * r;
    float C = dot(a, a) - r * r;
    float disc = B * B - 28.0 * C;
    if (disc >= 0.0) {
        float dc = (-B + sqrt(disc)) / 14.0;
        if (a.x + 2.0 * dc >= 0.0 && a.y + 2.0 * dc >= 0.0)
            d = dc;                                        // in a corner zone: the rounded contour
    }
    vec2 s = p - H + h;
    return -max(d, h - length(max(s, 0.0)) - min(max(s.x, s.y), 0.0));
}

// ============================================================================
// REFRACTION DIRECTION
// Pixel-space direction toward window center — perfectly smooth everywhere,
// no SDF gradient needed (optional edge-following blend below). On straight edges the perpendicular pixel distance
// dominates, giving approximately edge-normal direction. At corners it
// naturally follows the diagonal.
// ============================================================================

vec2 refractionDir(vec2 uv) {
    vec2 toCenterPx = (vec2(0.5) - uv) * fullSize;
    float len = length(toCenterPx);
    return len > 0.1 ? toCenterPx / len : vec2(0.0);
}

// Smooth edge-following field: points into the box, hugging each edge's normal
// away from the corners and blending crease-free through the diagonals.
vec2 edgeDir(vec2 posPx) {
    vec2 halfSize = fullSize * 0.5;
    vec2 n = abs(posPx) / halfSize;
    vec2 g = sign(posPx) * pow(n, vec2(7.0)) / halfSize;
    float len = length(g);
    return len > 0.0 ? -g / len : vec2(0.0);   // points INTO the box
}

// light direction for a clockwise angle in degrees, 0 = from the top (screen y grows downward)
vec2 lightDir(float angleDeg) {
    float a = radians(angleDeg);
    return vec2(sin(a), -cos(a));
}

// exact outward normal from the SDF gradient; only meaningful right at the edge
vec2 sdfOutwardNormal(vec2 uv) {
    vec2 h = vec2(1.0) / fullSize;
    vec2 grad = vec2(
        getCornerSDF(uv + vec2(h.x, 0.0)) - getCornerSDF(uv - vec2(h.x, 0.0)),
        getCornerSDF(uv + vec2(0.0, h.y)) - getCornerSDF(uv - vec2(0.0, h.y))
    );
    float len = length(grad);
    return len > 0.0 ? grad / len : vec2(0.0, -1.0);
}

// ============================================================================
// MAIN — Thick-glass refraction model
// ============================================================================

void main() {
    vec2 uv = v_texcoord;

    // Layers only: sample the temp FBO to get the rendered surface pixel.
    // Discard fully transparent fragments so glass only covers visible content.
    // For windows, hasMask is false and this block is skipped entirely.
    vec4 surfacePixel = vec4(0.0);
    bool hasMask = (useMask == 1);
    if (hasMask) {
        vec2 maskUV = uv * maskUVScale + maskUVOffset;
        surfacePixel = texture(maskTex, clamp(maskUV, 0.001, 0.999));

        if (maskMode == 1) {
            vec2 pixelPos = uv * fullSize;
            bool insideRegion = false;
            for (int i = 0; i < regionRectCount; i++) {
                vec4 r = regionRects[i];
                if (pixelPos.x >= r.x && pixelPos.y >= r.y &&
                    pixelPos.x <= r.x + r.z && pixelPos.y <= r.y + r.w) {
                    insideRegion = true;
                    break;
                }
            }
            // panelOnly: transparent black is a no-op under premultiplied blending;
            // discard skips the framebuffer read-modify-write for the same result.
            if (!insideRegion) { if (panelOnly == 1) discard; fragColor = surfacePixel; return; }
        } else if (surfacePixel.a < maskAlphaThreshold) {
            discard;
        }
    }

    bool silhouette = hasMask && useSilhouette == 1;
    float cornerSdf;
    float cornerAlpha;
    vec2  silInward    = vec2(0.0);
    float silCoherence = 0.0;
    if (silhouette) {
        float d = texture(sdfTex, uv).r * sdfPxScale;   // framebuffer px, positive inside
        cornerSdf = -d;
        // Coverage from the full-resolution surface alpha keeps edges crisp
        // (the field is lower resolution). The surface is composited over the
        // glass below; solve source-over so the combined coverage equals the
        // client's antialiased coverage instead of thickening corners:
        // surfA + (1 - surfA) * behind = coverage.
        float coverage = clamp(surfacePixel.a / max(silThreshold, 0.0001), 0.0, 1.0);
        cornerAlpha = panelOnly == 1 ? coverage : clamp((coverage - surfacePixel.a) / max(1.0 - surfacePixel.a, 0.0001), 0.0, 1.0);

        // Most pixels of a full-screen layer are not glass: leave before the
        // eight-tap normal estimate (debug views still show everything).
        if ((coverage < 0.001 || (cornerAlpha < 0.001 && innerCount == 0)) && silDebug == 0) {
            if (panelOnly == 1) discard;
            fragColor = surfacePixel;
            return;
        }

        // Sobel over three-texel taps: merges parallel differences instead of
        // trusting one pair of samples on a rasterized corner.
        vec2 ts = 1.0 / vec2(textureSize(sdfTex, 0));
        vec2 dx = vec2(3.0 * ts.x, 0.0);
        vec2 dy = vec2(0.0, 3.0 * ts.y);
        float tl = texture(sdfTex, uv - dx - dy).r;
        float tr = texture(sdfTex, uv + dx - dy).r;
        float bl = texture(sdfTex, uv - dx + dy).r;
        float br = texture(sdfTex, uv + dx + dy).r;
        vec2 g = vec2(tr + 2.0 * texture(sdfTex, uv + dx).r + br - tl - 2.0 * texture(sdfTex, uv - dx).r - bl,
                      bl + 2.0 * texture(sdfTex, uv + dy).r + br - tl - 2.0 * texture(sdfTex, uv - dy).r - tr) / 24.0;
        float mag = length(g);
        silInward = g / max(mag, 0.0001);
        // Opposing normals cancel where two shapes meet (a blob neck), and the
        // clamped interior has no gradient: attenuate rather than flip.
        silCoherence = smoothstep(0.15, 0.85, mag);

        if (silDebug > 0) {
            vec3 dbg = silDebug == 1 ? vec3(coverage)
                     : silDebug == 2 ? vec3(clamp(d / max(1.0 / invBezelWidthPx / 0.45, 1.0), 0.0, 1.0))
                     : vec3(0.5 + silInward * silCoherence * 0.5, 0.5);
            fragColor = vec4(dbg * coverage, coverage);
            return;
        }
    } else {
        cornerSdf   = getCornerSDF(uv);
        cornerAlpha = 1.0 - smoothstep(-1.5, 0.5, cornerSdf);
    }

    if (silhouette) {
        // handled above
    } else if (maskMode == 1) {
        // Subsurface items: the glass box (glassBoxOffsetPx/SizePx) can be
        // smaller than the item's own drawn box/region — e.g. a capsule glass
        // shape inside a rectangular blur region. Outside it, fall back to the
        // plain surface pixel instead of dropping it (a hard discard here would
        // silently delete client content, like an icon, that simply isn't under
        // the glass). cornerAlpha fades smoothly across the boundary (the same
        // curve already used for glassA below), so this is an antialiased
        // "glass * coverage, surface over" blend, not a hard cutover — the
        // actual blend happens in the hasMask composite at the bottom of main().
        if (cornerAlpha < 0.001) {
            fragColor = panelOnly == 1 ? vec4(0.0) : surfacePixel;
            return;
        }
    } else {
        // Windows, and alpha-mask layers: no surface pixel to fall back to
        // outside the glass shape, so this is exactly the old hard-edged cutoff.
        if (cornerSdf > 0.0) discard;
        if (cornerAlpha < 0.001) discard;
    }

    // Each explicitly registered inner surface gets its own rounded boundary.
    // Use the closest inner rim, not the smallest alpha feature (text/icons).
    float innerSdf = -1e6;
    vec2 innerNormal = vec2(0.0);
    float innerOpacity = 0.0;
    vec2 localPx = uv * fullSize;
    for (int i = 0; i < innerCount; ++i) {
        vec4 box = innerBoxes[i];
        vec4 clip = innerClips[i];
        if (localPx.x < clip.x || localPx.y < clip.y || localPx.x > clip.z || localPx.y > clip.w
            || localPx.x < box.x || localPx.y < box.y || localPx.x > box.x + box.z || localPx.y > box.y + box.w)
            continue;
        vec2 halfBox = box.zw * 0.5;
        vec2 p = localPx - box.xy - halfBox;
        vec4 r = innerRadii[i];
        float radius = p.x < 0.0 ? (p.y < 0.0 ? r.x : r.w) : (p.y < 0.0 ? r.y : r.z);
        radius = min(radius, min(halfBox.x, halfBox.y));
        vec2 q = abs(p) - halfBox + radius;
        float d = min(max(q.x, q.y), 0.0) + length(max(q, 0.0)) - radius;
        if (d > 0.0 || d <= innerSdf) continue;
        vec2 gradient = max(q, 0.0);
        if (length(gradient) < 0.001)
            gradient = q.x > q.y ? vec2(1.0, 0.0) : vec2(0.0, 1.0);
        innerNormal = -normalize(gradient) * sign(p);
        innerSdf = d;
        innerOpacity = innerOpacities[i];
    }
    float innerRim = min(8.0 * monitorScale, 12.0);
    bool innerEdge = innerSdf > -innerRim && innerOpacity > 0.04;
    if (innerEdge) {
        cornerSdf = innerSdf;
        silInward = innerNormal;
        silCoherence = innerOpacity;
        cornerAlpha = 1.0;
    }

    float minDim = min(fullSize.x, fullSize.y);
    // silhouette: the host sets invBezelWidthPx from the rim width, not the layer size
    float bezelWidthPx = silhouette ? 1.0 / invBezelWidthPx : edgeThickness * minDim;
    if (innerEdge) bezelWidthPx = innerRim * 0.45;

    // ========================================
    // EDGE PROXIMITY + DIRECTION
    // edgeProximity: 1.0 at boundary, exponential decay inward
    // inwardDir: pixel-space direction toward center (smooth everywhere)
    // Clamped to 1.0: cornerSdf can be positive here (maskMode==1's glass box
    // can be smaller than fullSize, so fragments just outside it still reach
    // this code with cornerAlpha > 0.001), and exp() of a positive value would
    // otherwise overshoot every effect that scales off edgeProximity.
    // ========================================
    // crease-free bevel distance, so the edge has no seam along the corner diagonals;
    // it assumes circular corners, so a superellipse outline keeps the exact SDF
    float bevelSdf = (silhouette || roundingPower != 2.0) ? cornerSdf : getBevelSDF(uv);
    float edgeProximity = min(exp(bevelSdf * invBezelWidthPx), 1.0);
    if (innerEdge) edgeProximity = exp(innerSdf / max(bezelWidthPx, 0.001));
    vec2 inwardDir = (silhouette || innerEdge) ? silInward : refractionDir(uv);
    vec2 posPx = (uv - 0.5) * fullSize; // pixel-space position for the edge-flow direction below

    // ========================================
    // EDGE REFRACTION
    // Offset sampling UV inward (toward center) at edges — like looking
    // through the curved thick edge of a glass slab. This compresses
    // and distorts what's already behind the window, without reaching
    // beyond the window boundary.
    // ========================================
    float refractionPx = refractionStrength * 50.0;
    // silhouette: never pull from further than the rim is wide (bezel = 0.45 rim),
    // so small controls refract their own surroundings, not content beyond them
    if (silhouette || innerEdge)
        refractionPx = min(refractionPx, bezelWidthPx / 0.45);
    float lensFalloff = edgeProximity;
    if (refractionSpread < 0.999) {
        // rim-only lens: window the exponential tail so the centre stays flat
        float depth = -bevelSdf;
        float tailWindow = 1.0 - smoothstep(1.5 * bezelWidthPx, 3.0 * bezelWidthPx, depth);
        lensFalloff = mix(edgeProximity * tailWindow, edgeProximity, refractionSpread);
    }
    float refractionMag = lensFalloff * refractionPx * ((silhouette || innerEdge) ? silCoherence : 1.0);
    vec2 dir = inwardDir;
    if (refractionFlow > 0.001 && !silhouette && !innerEdge) { // explicit normals already follow the edges
        // pull along the edges instead of toward the centre
        vec2 mixedDir = mix(inwardDir, edgeDir(posPx), refractionFlow);
        float mixedLen = length(mixedDir);
        dir = mixedLen > 0.0001 ? mixedDir / mixedLen : inwardDir;
    }
    vec2 baseOffset = dir * refractionMag * invFullSize;

    // ========================================
    // CHROMATIC ABERRATION — per-channel refraction scale
    // Blue refracts more than red → natural spectral fringing at edges.
    // ========================================
    float chromaSpread = chromaticAberration * 0.35;
    vec2 offsetR = baseOffset * (1.0 - chromaSpread);
    vec2 offsetG = baseOffset;
    vec2 offsetB = baseOffset * (1.0 + chromaSpread);

    // ========================================
    // CENTER DOME LENS (subtle magnification in the flat interior)
    // Fades near edges so it doesn't interfere with edge refraction.
    // ========================================
    vec2 domeUV = vec2(0.0);
    if (lensDistortion > 0.001) {
        vec2 c = (uv - 0.5) * 2.0;
        vec2 dGrad = vec2(
            -4.0 * c.x * (1.0 - c.y * c.y),
            -4.0 * c.y * (1.0 - c.x * c.x)
        );
        float lensFade = 1.0 - edgeProximity;
        domeUV = dGrad * lensMaxPx * lensFade * invFullSize;
    }

    // ========================================
    // BACKGROUND SAMPLING (frosted blur only)
    // Nearby color influence comes naturally from the Gaussian blur
    // kernel crossing the window boundary — no explicit raw sampling.
    // ========================================
    vec3 color;
    vec2 uvR = uv + offsetR + domeUV;
    vec2 uvG = uv + offsetG + domeUV;
    vec2 uvB = uv + offsetB + domeUV;

    if (chromaticAberration > 0.001 && edgeProximity > 0.01) {
        color.r = sampleBlurred(uvR).r;
        color.g = sampleBlurred(uvG).g;
        color.b = sampleBlurred(uvB).b;
    } else {
        color = sampleBlurred(uvG).rgb;
    }

    // ========================================
    // FROSTED TINT (per-theme tone mapping)
    // ========================================
    float blurredLum = dot(color, vec3(0.2126, 0.7152, 0.0722));

    // Frosted desaturation
    color = mix(vec3(blurredLum), color, saturation);

    // Tight smoothstep range maps the blur-compressed luminance (~0.3-0.7)
    // to the full [0,1] adaptive range, creating visible per-region differentiation
    float lumCurve = smoothstep(0.25, 0.55, blurredLum);

    // Dim: multiplicative — effective at darkening bright areas
    color *= brightness * (1.0 - adaptiveDim * lumCurve);

    // Boost: additive lift — multiplicative can't brighten near-black content
    color += vec3(adaptiveBoost * (1.0 - lumCurve) * 0.5);

    // Contrast (pivot around midpoint)
    color = mix(vec3(0.5), color, contrast);

    // Vibrancy (selective saturation boost scaled by existing saturation)
    float currentLum = dot(color, vec3(0.2126, 0.7152, 0.0722));
    float sat = max(color.r, max(color.g, color.b)) - min(color.r, min(color.g, color.b));
    float darkFactor = 1.0 - vibrancyDarkness * (1.0 - blurredLum);
    color = mix(vec3(currentLum), color, 1.0 + vibrancy * sat * darkFactor);

    // ========================================
    // COLOR TINT OVERLAY
    // ========================================
    color = mix(color, tintColor, tintAlpha);

    // ========================================
    // BEVEL — thin lit line hugging the edge, brightest on the side facing the light
    // ========================================
    if (bevelStrength > 0.001) {
        float sizePx = max(bevelSize * monitorScale, 1.0);   // logical px, uniform across monitor scales
        float core = 0.25 * sizePx;
        float tail = sizePx;
        float ring = (1.0 - smoothstep(-core, 0.0, cornerSdf)) * smoothstep(-tail, -core, cornerSdf);

        // lit side faces the light, the far side fades out and can be shadowed
        vec2 outward = (silhouette || innerEdge) ? -silInward : sdfOutwardNormal(uv);
        float facing = clamp(dot(outward, lightDir(bevelAngle)) * 0.5 + 0.5, 0.0, 1.0);

        vec3 bevelLight = vec3(1.0);
        if (bevelColorAlpha > 0.001) bevelLight = mix(vec3(1.0), bevelColor, bevelColorAlpha);   // a dark colour gives a dark line
        if (bevelTint > 0.001) {
            float maxC = max(max(color.r, color.g), color.b);
            bevelLight = mix(bevelLight, maxC > 0.001 ? color / maxC : vec3(1.0), bevelTint);
        }

        color = mix(color, bevelLight, ring * facing * bevelStrength);
        if (bevelShadow > 0.001)
            color = mix(color, vec3(0.0), ring * (1.0 - facing) * bevelShadow);
    }

    // ========================================
    // FRESNEL RIM GLOW (edge zone)
    // ========================================
    if (fresnelStrength > 0.001) {
        float fresnel = edgeProximity * edgeProximity * fresnelStrength * 0.15;
        vec3 fresnelLight = vec3(1.0);
        if (fresnelColorAlpha > 0.001) fresnelLight = mix(vec3(1.0), fresnelColor, fresnelColorAlpha);   // chosen colour, then the tint below
        if (fresnelTint > 0.001) {
            // rim light in the background's own hue, at full brightness so the gain matches white
            float maxC = max(max(color.r, color.g), color.b);
            fresnelLight = mix(fresnelLight, maxC > 0.001 ? color / maxC : vec3(1.0), fresnelTint);
        }
        color += fresnelLight * fresnel;
    }

    // ========================================
    // SPECULAR — subtle top highlight (edge zone)
    // ========================================
    if (specularStrength > 0.001) {
        float specT = max(1.0 - uv.y, 0.0);
        if (silhouette) {
            // per-shape: the lit rim is wherever the edge faces the light, not the top of the layer
            specT = clamp(dot(-silInward, lightDir(specularAngle)) * 0.5 + 0.5, 0.0, 1.0) * silCoherence;
        } else if (abs(specularAngle) > 0.001) {
            // rotate the highlight gradient toward the light: at angle 0, lightDir gives
            // (0,-1) and dot(uv-0.5, L) = 0.5-uv.y, so 0.5+dot(...) reduces to 1-uv.y exactly
            vec2 L = lightDir(specularAngle);
            specT = clamp(0.5 + dot(uv - 0.5, L), 0.0, 1.0);
        }
        float topBias = pow(specT, 2.0);
        float spec = topBias * edgeProximity * edgeProximity * specularStrength * 0.08;
        color += vec3(1.0, 0.99, 0.97) * spec;
    }

    // ========================================
    // INNER SHADOW (bottom rim)
    // ========================================
    {
        float bottomBias = silhouette
            ? pow(clamp(dot(-silInward, vec2(0.0, 1.0)) * 0.5 + 0.5, 0.0, 1.0), 2.0) * silCoherence
            : pow(uv.y, 2.0);
        float shadow = bottomBias * edgeProximity * edgeProximity * 0.06;
        color *= 1.0 - shadow;
    }

    // float framebuffers (FP16 under wide-gamut cm) store unbounded values and
    // the glass re-samples its own output: unclamped color diverges over frames
    color = clamp(color, 0.0, 1.0);
    float glassA = clamp(glassOpacity * cornerAlpha, 0.0, 1.0);

    if (hasMask && panelOnly == 0) {
        // Layers only: composite the rendered surface over the glass effect
        // in a single pass. surfacePixel is premultiplied alpha from Hyprland's
        // surface rendering, so we unpremultiply before the 'over' blend.
        float surfA = surfacePixel.a;
        // Let solid accent controls reveal their lens at the boundary as well.
        // Labels remain untouched: their pixels are well inside the narrow rim.
        if (innerEdge) surfA *= 1.0 - 0.32 * edgeProximity * innerOpacity;
        vec3 surfRGB = surfacePixel.a > 0.001 ? surfacePixel.rgb / surfacePixel.a : vec3(0.0);

        float compA = surfA + glassA * (1.0 - surfA);
        vec3 compRGB = compA > 0.001
            ? (surfRGB * surfA + color * glassA * (1.0 - surfA)) / compA
            : vec3(0.0);

        // Hyprland's compositor expects premultiplied alpha (blend GL_ONE, GL_ONE_MINUS_SRC_ALPHA).
        fragColor = vec4(compRGB * compA, compA);
    } else {
        // Windows: output the glass effect alone, surface is rendered separately by Hyprland.
        // Premultiplied: without this, a fading window's glass keeps full RGB contribution
        // because the GL_ONE source factor adds raw color regardless of alpha.
        fragColor = vec4(color * glassA, glassA);
    }
}
)GLSL"},

    // Native controls: full-resolution analytical lenses. Geometry is identical
    // for all presets; only material uniforms vary. Based on ShojiWM's circular
    // profile (MIT), never on a control's client alpha or half-resolution field.
    {"liquidshape.frag", R"GLSL(
#version 300 es
precision highp float;
uniform sampler2D tex;
uniform sampler2D softTex;
uniform vec2 shapeSize, sampleOffset, sampleSize, storageSize;
uniform vec4 radii, tint;
uniform float bezel, opacity, frostMix, specular; // bezel = largest side band
uniform vec4 bezelSides; // per-side band px: top, right, bottom, left (child clearance)
uniform int inlineFrost;
uniform float inlineRadius;
// Shared GlassShapeLens.hpp material: displacement per px of band, chromatic shift px, lip px.
uniform vec3 lensPhysical;
uniform int debugView;
in vec2 v_texcoord;
layout(location = 0) out vec4 fragColor;

const float PROFILE_SOFTNESS_PX = 0.5;
const float PROFILE_OUTER_TAP_PX = 0.375;
const float PROFILE_INNER_TAP_PX = 0.125;
const float NORMAL_STEP_PX = 0.5;
const float AA_HALF_PIXEL = 0.5;
const float DEEP_LIP_E_FOLDS = 32.0;
const float CHROMATIC_SUPPORT_FRACTION = 0.5;
const float MIN_CHROMATIC_SUPPORT_PX = 1.0;
float localFrost;

float roundedSDF(vec2 p) {
    vec2 halfSize = shapeSize * 0.5;
    float r = p.x < 0.0 ? (p.y < 0.0 ? radii.x : radii.w) : (p.y < 0.0 ? radii.y : radii.z);
    vec2 q = abs(p) - halfSize + r;
    return min(max(q.x, q.y), 0.0) + length(max(q, 0.0)) - r;
}
float circularLens(float d, float b) {
    float x = 1.0 - clamp(d / b, 0.0, 1.0);
    float epsilon = clamp(PROFILE_SOFTNESS_PX / b, 0.0001, 0.5);
    float top = sqrt(1.0 + epsilon);
    return (top - sqrt(max(1.0 - x*x, 0.0) + epsilon)) / (top - sqrt(epsilon));
}
float profile(float d, float b) {
    return 0.25 * (circularLens(d - PROFILE_OUTER_TAP_PX, b) + circularLens(d - PROFILE_INNER_TAP_PX, b)
                 + circularLens(d + PROFILE_INNER_TAP_PX, b) + circularLens(d + PROFILE_OUTER_TAP_PX, b));
}
vec3 inlineSoftAt(vec2 p) {
    // Exact ideal equivalent of binomial blur at texel centers followed by
    // bilinear soft-texture sampling, including fractional warped coordinates.
    // Sampling four taps simply at p +/- r/2 is NOT equivalent at lens edges.
    // The effective 1-D footprint has four weights; group adjacent pairs into
    // two hardware-linear reads per axis, hence four reads for the 2-D kernel.
    vec2 center = floor(p - 0.5) + 0.5;
    vec2 fraction = p - center;
    float side = inlineRadius * 0.25;
    float middle = 1.0 - inlineRadius * 0.5;
    vec2 a = (1.0 - fraction) * side;
    vec2 b = (1.0 - fraction) * middle + fraction * side;
    vec2 c = (1.0 - fraction) * side + fraction * middle;
    vec2 d = fraction * side;
    vec2 first = a + b, second = c + d;
    vec2 lo = clamp(center - 1.0 + b / max(first, vec2(0.00001)), vec2(0.5), sampleSize - 0.5) / storageSize;
    vec2 hi = clamp(center + 1.0 + d / max(second, vec2(0.00001)), vec2(0.5), sampleSize - 0.5) / storageSize;
    return texture(tex, lo).rgb * (first.x * first.y)
         + texture(tex, vec2(hi.x, lo.y)).rgb * (second.x * first.y)
         + texture(tex, vec2(lo.x, hi.y)).rgb * (first.x * second.y)
         + texture(tex, hi).rgb * (second.x * second.y);
}
vec3 backgroundAt(vec2 p) {
    p = clamp(p, vec2(0.5), sampleSize - 0.5);
    vec2 uv = p / storageSize;
    if (localFrost <= 0.0) return texture(tex, uv).rgb;
    if (inlineFrost == 1) return mix(texture(tex, uv).rgb, inlineSoftAt(p), localFrost);
    if (localFrost >= 1.0) return texture(softTex, uv).rgb;
    return mix(texture(tex, uv).rgb, texture(softTex, uv).rgb, localFrost);
}
void main() {
    vec2 local = v_texcoord * shapeSize;
    vec2 p = local - shapeSize * 0.5;
    float sdf = roundedSDF(p);
    float coverage = 1.0 - smoothstep(-AA_HALF_PIXEL, AA_HALF_PIXEL, sdf);
    if (coverage <= 0.0) discard;
    float d = max(-sdf, 0.0);
    float lip = lensPhysical.z;
    // Effective band for this pixel: blend the side bands by edge direction
    // (exact on straight edges, smooth through corners). Local y grows down.
    vec2 gradB = vec2(roundedSDF(p + vec2(NORMAL_STEP_PX, 0.0)) - roundedSDF(p - vec2(NORMAL_STEP_PX, 0.0)),
                      roundedSDF(p + vec2(0.0, NORMAL_STEP_PX)) - roundedSDF(p - vec2(0.0, NORMAL_STEP_PX)));
    vec2 outN = gradB / max(length(gradB), 0.0001);
    float wx = abs(outN.x), wy = abs(outN.y);
    float bx = outN.x < 0.0 ? bezelSides.w : bezelSides.y;
    float by = outN.y < 0.0 ? bezelSides.x : bezelSides.z;
    // Uniform bands (no child clearance) take the exact shared band.
    float b = all(equal(bezelSides, vec4(bezel))) ? bezel : (wx + wy) > 0.0001 ? (bx * wx + by * wy) / (wx + wy) : bezel;
    // Clear analytical edge sampling is shared across presets. Frost returns
    // through the physical rim, rather than obscuring the actual displacement.
    localFrost = debugView >= 4 ? 0.0 : frostMix * smoothstep(0.0, b, d);
    // The filtered circular lens is EXACTLY zero beyond b+0.375. Deep in
    // large parents/cards the lip tails are below 1.3e-14 too (32 e-folds),
    // immaterial even to FP16. Avoid four additional SDFs, eight sqrt profiles,
    // exp/pow, normals and three repeated RGB fetches for this common interior.
    // Debug views retain their complete SDF/normal/bezel semantics.
    if (debugView == 0 && d >= max(bezel + PROFILE_OUTER_TAP_PX, DEEP_LIP_E_FOLDS * max(lip, AA_HALF_PIXEL))) {
        vec3 interior = mix(backgroundAt(sampleOffset + local), tint.rgb, tint.a);
        float a = coverage * opacity;
        fragColor = vec4(interior * a, a);
        return;
    }
    // Finite differences of the exact SDF work at asymmetric corner joins and
    // at the box's axes, where sign(p) gradients can otherwise vanish.
    vec2 grad = vec2(roundedSDF(p + vec2(NORMAL_STEP_PX, 0.0)) - roundedSDF(p - vec2(NORMAL_STEP_PX, 0.0)),
                     roundedSDF(p + vec2(0.0, NORMAL_STEP_PX)) - roundedSDF(p - vec2(0.0, NORMAL_STEP_PX)));
    float magnitude = length(grad);
    vec2 inward = -grad / max(magnitude, 0.0001);
    float lens = 0.0;
    vec3 color;
    if (debugView == 4 || d >= b + PROFILE_OUTER_TAP_PX) {
        // Keep every representable lip reflection/shadow outside the bezel,
        // but the three chromatic samples have exactly identical coordinates.
        color = backgroundAt(sampleOffset + local);
    } else {
        lens = profile(d, b);
        vec2 bend = inward * lens * lensPhysical.x * b;
        vec2 dispersion = inward * profile(d, max(CHROMATIC_SUPPORT_FRACTION * b, MIN_CHROMATIC_SUPPORT_PX)) * lensPhysical.y;
        vec2 samplePx = sampleOffset + local + bend;
        color = vec3(backgroundAt(samplePx + dispersion).r, backgroundAt(samplePx).g,
                     backgroundAt(samplePx - dispersion).b);
    }
    float light = dot(-inward, normalize(vec2(-0.45, -0.89)));
    float rim = exp(-d / max(lip, 0.5));
    float inset = exp(-pow((d - 0.5 * lip) / max(0.45 * lip, 0.25), 2.0));
    if (debugView < 4) {
        color *= 1.0 - 0.08 * inset * (0.5 + 0.5 * max(-light, 0.0));
        color += vec3(0.94, 0.97, 1.0) * specular
               * (rim * (0.3 + 0.7 * max(light, 0.0)) + inset * 0.22 * max(light, 0.0));
    }
    color = mix(color, tint.rgb, tint.a);
    if (debugView == 1) color = vec3(clamp(0.5 - sdf / max(b * 2.0, 1.0), 0.0, 1.0));
    if (debugView == 2) color = vec3(0.5 + inward * 0.5, 0.5);
    if (debugView == 3) color = vec3(lens, step(d, b), rim);
    float a = coverage * opacity;
    fragColor = vec4(color * a, a);
}
)GLSL"},

    // One small 2-D binomial frost pass per sampled region, not two separable
    // monitor-wide passes. Bounds refer to initialized pixels, not FBO capacity.
    {"shapefrost.frag", R"GLSL(
#version 300 es
precision highp float;
uniform sampler2D tex;
uniform vec2 sampleSize, storageSize;
uniform float radius;
in vec2 v_texcoord;
layout(location = 0) out vec4 fragColor;
void main() {
    vec2 p = v_texcoord * sampleSize;
    if (radius <= 1.0) {
        // At framebuffer pixel centers, the 3x3 [1,2,1] kernel with tap
        // spacing r<=1 equals four bilinear taps at (+/-r/2,+/-r/2).
        // Only frost is filtered; sharp snapshots and lens geometry stay full
        // resolution. Larger radii keep the original nine-tap material.
        vec2 delta = vec2(radius * 0.5);
        vec2 low = vec2(0.5), high = sampleSize - 0.5;
        fragColor = 0.25 * (texture(tex, clamp(p - delta, low, high) / storageSize)
                         + texture(tex, clamp(p + vec2(delta.x, -delta.y), low, high) / storageSize)
                         + texture(tex, clamp(p + vec2(-delta.x, delta.y), low, high) / storageSize)
                         + texture(tex, clamp(p + delta, low, high) / storageSize));
        return;
    }
    vec4 sum = vec4(0.0);
    for (int y = -1; y <= 1; ++y) for (int x = -1; x <= 1; ++x) {
        float weight = (x == 0 ? 2.0 : 1.0) * (y == 0 ? 2.0 : 1.0);
        vec2 uv = clamp(p + vec2(float(x), float(y)) * radius, vec2(0.5), sampleSize - 0.5) / storageSize;
        sum += texture(tex, uv) * weight;
    }
    fragColor = sum / 16.0;
}
)GLSL"},

    {"shapeforeground.frag", R"GLSL(
#version 300 es
precision highp float;
uniform sampler2D tex;
uniform vec2 uvOffset, uvScale;
in vec2 v_texcoord;
layout(location = 0) out vec4 fragColor;
void main() {
    // Already premultiplied and faded by Hyprland. No second fade, mask,
    // unpremultiply, tint, threshold or warp may touch client labels/icons.
    fragColor = texture(tex, v_texcoord * uvScale + uvOffset);
}
)GLSL"},

    // ── Silhouette shape mode (SilhouetteField.cpp) ─────────────────────────
    // Distance field from a layer's own alpha, after ShojiWM's island glass
    // pipeline (MIT, github.com/bea4dev/ShojiWM, packages/config/src/effect).
    // All passes render into RGBA16F field textures; distances are field px.

    {"silhouette_mask.frag", R"GLSL(
#version 300 es
precision highp float;

uniform sampler2D tex;          // layer temp FBO (premultiplied), monitor sized
uniform vec2 maskUVOffset;      // field UV -> temp FBO UV, same mapping the glass shader uses
uniform vec2 maskUVScale;
uniform vec2 boxSizePx;         // layer box in framebuffer px (region rects are box-local)
uniform float threshold;        // alpha of the client's glass tint: at or above it a pixel is fully inside
uniform int regionRectCount;    // 0 = no region gating
uniform vec4 regionRects[16];

in vec2 v_texcoord;
layout(location = 0) out vec4 fragColor;

void main() {
    vec2 uv = v_texcoord;
    vec4 surface = texture(tex, clamp(uv * maskUVScale + maskUVOffset, 0.0005, 0.9995));
    float a = surface.a;
    // Coverage, not a hard step: the client's antialiased tint edge stays a
    // sub-pixel boundary for the seed pass. Text inside the tint clamps to 1.
    float coverage = clamp(a / max(threshold, 0.0001), 0.0, 1.0);
    // G: anything at all here (any channel, ungated), for the tile list. A
    // flag, so content changes inside covered areas leave the mask unchanged.
    float visible = max(max(surface.r, surface.g), max(surface.b, surface.a)) > 0.0 ? 1.0 : 0.0;

    if (regionRectCount > 0) {
        vec2 p = uv * boxSizePx;
        bool inside = false;
        for (int i = 0; i < regionRectCount; i++) {
            vec4 r = regionRects[i];
            if (p.x >= r.x && p.y >= r.y && p.x <= r.x + r.z && p.y <= r.y + r.w) {
                inside = true;
                break;
            }
        }
        if (!inside)
            coverage = 0.0;
    }

    fragColor = vec4(coverage, visible, 0.0, 1.0);
}
)GLSL"},

    {"silhouette_seed.frag", R"GLSL(
#version 300 es
precision highp float;

uniform sampler2D tex; // coverage field
in vec2 v_texcoord;
layout(location = 0) out vec4 fragColor;

float maskAt(vec2 uv) {
    if (any(lessThan(uv, vec2(0.0))) || any(greaterThan(uv, vec2(1.0))))
        return 0.0;
    return texture(tex, uv).r;
}

void main() {
    // Texel centre from gl_FragCoord: exact, and the same for a full quad or a tile.
    vec2 pixel = 1.0 / vec2(textureSize(tex, 0));
    vec2 uv = gl_FragCoord.xy * pixel;
    float center = maskAt(uv);
    float left   = maskAt(uv - vec2(pixel.x, 0.0));
    float right  = maskAt(uv + vec2(pixel.x, 0.0));
    float top    = maskAt(uv - vec2(0.0, pixel.y));
    float bottom = maskAt(uv + vec2(0.0, pixel.y));
    float low  = min(center, min(min(left, right), min(top, bottom)));
    float high = max(center, max(max(left, right), max(top, bottom)));
    if (low >= 0.5 || high < 0.5) {
        fragColor = vec4(0.0);
        return;
    }
    // RG: sub-pixel offset to the 0.5 iso-line, B: valid seed
    vec2 gradient = 0.5 * vec2(right - left, bottom - top);
    float magnitude = length(gradient);
    vec2 offset = gradient / max(magnitude, 0.0001) * clamp((center - 0.5) / max(magnitude, 0.0001), -1.0, 1.0);
    fragColor = vec4(-offset, 1.0, 1.0);
}
)GLSL"},

    {"silhouette_jump.frag", R"GLSL(
#version 300 es
precision highp float;

uniform sampler2D tex; // previous jump-flood field
uniform float jumpPx;
in vec2 v_texcoord;
layout(location = 0) out vec4 fragColor;

void main() {
    vec2 size = vec2(textureSize(tex, 0));
    vec2 p = gl_FragCoord.xy; // texel centre: candidates must be fetched at texel centres
    vec4 best = vec4(0.0);
    float bestDistance = 1.0e20;
    for (int y = -1; y <= 1; y++) {
        for (int x = -1; x <= 1; x++) {
            vec2 q = p + vec2(float(x), float(y)) * jumpPx;
            if (any(lessThan(q, vec2(0.5))) || any(greaterThan(q, size - 0.5)))
                continue;
            vec4 candidate = texture(tex, q / size);
            if (candidate.b < 0.5)
                continue;
            vec2 delta = candidate.rg + (q - p);
            float d2 = dot(delta, delta);
            if (d2 < bestDistance) {
                bestDistance = d2;
                best = vec4(delta, 1.0, 1.0);
            }
        }
    }
    fragColor = best;
}
)GLSL"},

    {"silhouette_distance.frag", R"GLSL(
#version 300 es
precision highp float;

uniform sampler2D tex;        // final jump-flood field
uniform sampler2D silhouette; // coverage field
uniform float limitPx;        // rim width in field px: nothing deeper needs a distance
in vec2 v_texcoord;
layout(location = 0) out vec4 fragColor;

void main() {
    vec2 size = vec2(textureSize(tex, 0));
    vec2 uv = gl_FragCoord.xy / size;
    vec4 nearest = texture(tex, uv);
    float d = nearest.b < 0.5 ? limitPx : min(length(nearest.rg), limitPx);
    float mask = texture(silhouette, uv).r;
    fragColor = vec4(mask >= 0.5 ? d : -d, 0.0, 0.0, 1.0); // signed, positive inside
}
)GLSL"},

    // Change test for conditional rebuilds: a fragment survives only where the
    // new coverage mask differs from the one the current field was built from.
    {"silhouette_diff.frag", R"GLSL(
#version 320 es
precision highp float;
uniform sampler2D tex;      // new coverage mask
uniform sampler2D previous; // mask of the last build
// Indirect arguments (uint offsets): 0-3 legacy draw, 4-6 tile-list reset
// dispatch, 7-9 tile-list dispatch, 10-12 change-area dispatch, 16+4k draw of
// field pass k. Nothing runs that the change test did not raise from 0.
layout(std430, binding = 0) buffer DrawArgs { uint args[]; };
// Field tiles (8x8 texels) holding a changed texel, marked with this build's generation.
layout(std430, binding = 1) buffer Dirty { uint dirty[]; };
uniform uvec2 tileGroups;      // coverage tile-list dispatch size
uniform uvec2 fieldTileGroups; // change-area dispatch size
uniform uint fieldTilesX;
uniform uint generation;
in vec2 v_texcoord;
layout(location = 0) out vec4 fragColor;
void main() {
    ivec2 p = ivec2(gl_FragCoord.xy);
    if (texelFetch(tex, p, 0).rg == texelFetch(previous, p, 0).rg)
        discard;
    atomicOr(args[1], 1u);
    args[4] = 1u;
    args[7] = tileGroups.x;
    args[8] = tileGroups.y;
    args[10] = fieldTileGroups.x;
    args[11] = fieldTileGroups.y;
    dirty[uint(p.y / 8) * fieldTilesX + uint(p.x / 8)] = generation;
    fragColor = vec4(1.0);
}
)GLSL"},

    // Tile list: 16x16 box-px tiles holding any non-zero surface pixel. With
    // the field at exactly half resolution, every mask texel samples the mean
    // of one 2x2 pixel block, so a tile of only-zero flags has only (0,0,0,0)
    // pixels: the panel shader discards all of them (no coverage) and the
    // foreground adds nothing there. Only listed tiles are drawn.
    {"silhouette_tiles_reset.comp", R"GLSL(
#version 320 es
layout(local_size_x = 1) in;
layout(std430, binding = 2) buffer Tiles { uint vertexCount; uint instanceCount; uint first; uint baseInstance; uint list[]; };
void main() {
    vertexCount = 4u;
    instanceCount = 0u;
    first = 0u;
    baseInstance = 0u;
}
)GLSL"},

    {"silhouette_tiles.comp", R"GLSL(
#version 320 es
precision highp float;
layout(local_size_x = 8, local_size_y = 8) in;
uniform highp sampler2D mask;
uniform uvec2 tileCount;
layout(std430, binding = 2) buffer Tiles { uint vertexCount; uint instanceCount; uint first; uint baseInstance; uint list[]; };
void main() {
    uvec2 t = gl_GlobalInvocationID.xy;
    if (t.x >= tileCount.x || t.y >= tileCount.y)
        return;
    ivec2 size = textureSize(mask, 0);
    ivec2 base = ivec2(t) * 8;
    bool covered = false;
    for (int y = 0; y < 8 && !covered; y++)
        for (int x = 0; x < 8; x++) {
            ivec2 p = base + ivec2(x, y);
            if (p.x < size.x && p.y < size.y && texelFetch(mask, p, 0).g > 0.0) {
                covered = true;
                break;
            }
        }
    if (covered)
        list[atomicAdd(instanceCount, 1u)] = t.x | (t.y << 16);
}
)GLSL"},

    // Vertex stage of the panel glass drawn as a tile list: one instance per
    // listed tile, same box UV (v_texcoord) as the full quad.
    {"liquidglass_tiles.vert", R"GLSL(
#version 320 es
uniform mat3 proj;
uniform vec2 tileBoxPx;
uniform float tileSizePx;
layout(std430, binding = 2) readonly buffer Tiles { uint vertexCount; uint instanceCount; uint first; uint baseInstance; uint list[]; };
in vec2 pos;
in vec2 texcoord;
out vec2 v_texcoord;
void main() {
    uint t = list[gl_InstanceID];
    vec2 tile = vec2(float(t & 0xffffu), float(t >> 16));
    vec2 p = min((tile + pos) * tileSizePx / tileBoxPx, vec2(1.0));
    v_texcoord = p;
    gl_Position = vec4(proj * vec3(p, 1.0), 1.0);
}
)GLSL"},

    // Change area, part 1: per field tile, the distance (in tiles) to the
    // nearest changed tile in its row, capped at maxDist + 1.
    {"silhouette_rowdist.comp", R"GLSL(
#version 320 es
layout(local_size_x = 8, local_size_y = 8) in;
uniform uvec2 tiles;
uniform uint generation;
uniform int maxDist;
layout(std430, binding = 1) readonly buffer Dirty { uint dirty[]; };
layout(std430, binding = 4) writeonly buffer RowDist { uint rowDist[]; };
void main() {
    uvec2 t = gl_GlobalInvocationID.xy;
    if (t.x >= tiles.x || t.y >= tiles.y)
        return;
    int best = maxDist + 1;
    for (int dx = -maxDist; dx <= maxDist; dx++) {
        int x = int(t.x) + dx;
        if (x >= 0 && x < int(tiles.x) && dirty[t.y * tiles.x + uint(x)] == generation)
            best = min(best, abs(dx));
    }
    rowDist[t.y * tiles.x + t.x] = uint(best);
}
)GLSL"},

    // Change area, part 2: Chebyshev tile distance to the nearest changed tile;
    // a tile joins the draw list of every field pass whose reach covers it.
    {"silhouette_lists.comp", R"GLSL(
#version 320 es
layout(local_size_x = 8, local_size_y = 8) in;
uniform uvec2 tiles;
uniform int maxDist;
uniform int passCount;
uniform int thresholds[16];
uniform uint listStride;
layout(std430, binding = 0) buffer DrawArgs { uint args[]; };
layout(std430, binding = 3) writeonly buffer Lists { uint list[]; };
layout(std430, binding = 4) readonly buffer RowDist { uint rowDist[]; };
void main() {
    uvec2 t = gl_GlobalInvocationID.xy;
    if (t.x >= tiles.x || t.y >= tiles.y)
        return;
    int d = maxDist + 1;
    for (int dy = -maxDist; dy <= maxDist; dy++) {
        int y = int(t.y) + dy;
        if (y >= 0 && y < int(tiles.y))
            d = min(d, max(int(rowDist[uint(y) * tiles.x + t.x]), abs(dy)));
    }
    for (int k = 0; k < passCount; k++)
        if (d <= thresholds[k])
            list[uint(k) * listStride + atomicAdd(args[17u + 4u * uint(k)], 1u)] = t.x | (t.y << 16);
}
)GLSL"},

    // Vertex stage of a field pass drawn over a tile list (8x8 field texels per instance).
    {"silhouette_tiles.vert", R"GLSL(
#version 320 es
uniform mat3 proj;
uniform vec2 fieldSize;
uniform uint listBase;
layout(std430, binding = 3) readonly buffer Lists { uint list[]; };
in vec2 pos;
in vec2 texcoord;
out vec2 v_texcoord;
void main() {
    uint t = list[listBase + uint(gl_InstanceID)];
    vec2 tile = vec2(float(t & 0xffffu), float(t >> 16));
    vec2 p = min((tile + pos) * 8.0 / fieldSize, vec2(1.0));
    v_texcoord = p;
    gl_Position = vec4(proj * vec3(p, 1.0), 1.0);
}
)GLSL"},

    {"silhouette_copy.frag", R"GLSL(
#version 300 es
precision highp float;
uniform sampler2D tex;
layout(location = 0) out vec4 fragColor;
void main() {
    fragColor = texelFetch(tex, ivec2(gl_FragCoord.xy), 0);
}
)GLSL"},

    {"silhouette_smooth.frag", R"GLSL(
#version 300 es
precision highp float;

uniform sampler2D tex;
uniform vec2 axis;
uniform float smoothingPx;
in vec2 v_texcoord;
layout(location = 0) out vec4 fragColor;

void main() {
    vec2 uv = gl_FragCoord.xy / vec2(textureSize(tex, 0));
    vec2 delta = smoothingPx * axis / vec2(textureSize(tex, 0));
    float d = texture(tex, uv).r * 0.375;
    d += (texture(tex, uv - delta).r + texture(tex, uv + delta).r) * 0.25;
    d += (texture(tex, uv - delta * 2.0).r + texture(tex, uv + delta * 2.0).r) * 0.0625;
    fragColor = vec4(d, 0.0, 0.0, 1.0);
}
)GLSL"},

    {"gaussianblur.frag", R"GLSL(
#version 300 es
precision highp float;

uniform sampler2D tex;
uniform vec2 direction; // (1.0/width, 0.0) for horizontal, (0.0, 1.0/height) for vertical
uniform float blurRadius; // kernel radius in pixels

in vec2 v_texcoord;
layout(location = 0) out vec4 fragColor;

void main() {
    // Compute sigma from radius (covers ~3 sigma)
    float sigma = max(blurRadius / 3.0, 0.001);
    float invSigma2 = -0.5 / (sigma * sigma);

    int samples = min(int(ceil(blurRadius)), 8);

    // Center tap, clamped: an out-of-range texel from a float framebuffer would otherwise dominate the kernel
    float w0 = 1.0;
    vec4 result = clamp(texture(tex, v_texcoord), 0.0, 1.0) * w0;
    float totalWeight = w0;

    // Linear sampling: pair adjacent taps (i, i+1) into a single bilinear fetch.
    // The interpolated offset between two texels yields their weighted average
    // in one texture() call, halving the total tap count.
    for (int i = 1; i <= samples; i += 2) {
        float x1 = float(i);
        float x2 = float(i + 1);
        float w1 = exp(x1 * x1 * invSigma2);
        float w2 = (i + 1 <= samples) ? exp(x2 * x2 * invSigma2) : 0.0;
        float wSum = w1 + w2;
        if (wSum < 0.0001) continue;

        // Offset biased toward the heavier weight
        float offset = (x1 * w1 + x2 * w2) / wSum;

        result += clamp(texture(tex, v_texcoord + direction * offset), 0.0, 1.0) * wSum;
        result += clamp(texture(tex, v_texcoord - direction * offset), 0.0, 1.0) * wSum;
        totalWeight += 2.0 * wSum;
    }

    fragColor = result / totalWeight;
}
)GLSL"},
};
