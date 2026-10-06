#version 440
// Applestia "inner glass" material: a faint frosted fill with a specular rim
// lit from one side, a dimmer back-reflection on the opposite edge and a soft
// top sheen. Approximates the compositor's Liquid Glass rim for elements that
// sit on top of a glass panel (QML cannot see the desktop to refract it).

layout(location = 0) in vec2 qt_TexCoord0;
layout(location = 0) out vec4 fragColor;

layout(std140, binding = 0) uniform buf {
    mat4 qt_Matrix;
    float qt_Opacity;
    vec2 size;          // item size, logical px
    vec4 radii;         // top-left, top-right, bottom-right, bottom-left
    vec4 fill;          // straight (non-premultiplied) rgba
    float rimStrength;  // brightness of the lit rim
    float sheen;        // top-to-bottom gloss
    vec2 lightDir;      // unit vector pointing TOWARDS the light (screen space, y down)
    float rimWidth;     // logical px
    float rimTransmission; // reduce the flat material inside the native lens band
};

float sdRoundBox(vec2 p, vec2 halfSize, vec4 r) {
    // pick the corner radius for this quadrant
    float rr = p.x < 0.0 ? (p.y < 0.0 ? r.x : r.w) : (p.y < 0.0 ? r.y : r.z);
    rr = min(rr, min(halfSize.x, halfSize.y));
    vec2 q = abs(p) - halfSize + rr;
    return min(max(q.x, q.y), 0.0) + length(max(q, 0.0)) - rr;
}

void main() {
    vec2 halfSize = size * 0.5;
    vec2 p = qt_TexCoord0 * size - halfSize;
    float d = sdRoundBox(p, halfSize, radii); // < 0 inside

    float coverage = clamp(0.5 - d, 0.0, 1.0);
    if (coverage <= 0.0) {
        fragColor = vec4(0.0);
        return;
    }

    // outward normal from the SDF gradient
    vec2 e = vec2(0.5, 0.0);
    vec2 n = vec2(sdRoundBox(p + e.xy, halfSize, radii) - sdRoundBox(p - e.xy, halfSize, radii),
                  sdRoundBox(p + e.yx, halfSize, radii) - sdRoundBox(p - e.yx, halfSize, radii));
    n = n / max(length(n), 1e-4);

    float inside = max(-d, 0.0);
    float band = exp(-inside / max(rimWidth, 0.5));          // 1 at the edge, fades inward
    float facing = dot(n, lightDir);
    float lit = band * (0.18 + 0.82 * max(facing, 0.0));       // lit edge
    float back = band * max(-facing, 0.0) * 0.35;              // internal reflection opposite
    float gloss = sheen * (1.0 - smoothstep(0.0, 0.55, qt_TexCoord0.y));

    float materialAlpha = fill.a * (1.0 - rimTransmission * exp(-inside / 3.5));
    vec3 rgb = fill.rgb * materialAlpha;
    float a = materialAlpha;
    float light = (lit + back) * rimStrength + gloss;
    rgb += vec3(light);
    a = clamp(a + light, 0.0, 1.0);

    fragColor = vec4(rgb, a) * coverage * qt_Opacity;
}
