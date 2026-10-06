#version 300 es
// Unoptimized shared physical lens: oracle, not shipped/compiled by plugin.
precision highp float;
uniform sampler2D tex;
uniform sampler2D softTex;
uniform vec2 shapeSize, sampleOffset, sampleSize, storageSize;
uniform vec4 radii, tint;
uniform float bezel, scale, opacity, frostMix, specular;
uniform vec3 lensPhysical;
uniform int debugView;
in vec2 v_texcoord;
layout(location = 0) out vec4 fragColor;
float localFrost;
float roundedSDF(vec2 p) {
    vec2 halfSize = shapeSize * 0.5;
    float r = p.x < 0.0 ? (p.y < 0.0 ? radii.x : radii.w) : (p.y < 0.0 ? radii.y : radii.z);
    vec2 q = abs(p) - halfSize + r;
    return min(max(q.x, q.y), 0.0) + length(max(q, 0.0)) - r;
}
float circularLens(float d, float b) {
    float x = 1.0 - clamp(d / b, 0.0, 1.0);
    float epsilon = clamp(0.5 / b, 0.0001, 0.5);
    float top = sqrt(1.0 + epsilon);
    return (top - sqrt(max(1.0 - x*x, 0.0) + epsilon)) / (top - sqrt(epsilon));
}
float profile(float d, float b) {
    return 0.25 * (circularLens(d - 0.375, b) + circularLens(d - 0.125, b)
                 + circularLens(d + 0.125, b) + circularLens(d + 0.375, b));
}
vec3 backgroundAt(vec2 p) {
    vec2 uv = clamp(p, vec2(0.5), sampleSize - 0.5) / storageSize;
    return mix(texture(tex, uv).rgb, texture(softTex, uv).rgb, localFrost);
}
void main() {
    vec2 local = v_texcoord * shapeSize;
    vec2 p = local - shapeSize * 0.5;
    float sdf = roundedSDF(p);
    float coverage = 1.0 - smoothstep(-0.5, 0.5, sdf);
    if (coverage <= 0.0) discard;
    vec2 grad = vec2(roundedSDF(p + vec2(0.5, 0.0)) - roundedSDF(p - vec2(0.5, 0.0)),
                     roundedSDF(p + vec2(0.0, 0.5)) - roundedSDF(p - vec2(0.0, 0.5)));
    float magnitude = length(grad);
    vec2 inward = -grad / max(magnitude, 0.0001);
    float d = max(-sdf, 0.0);
    localFrost = debugView >= 4 ? 0.0 : frostMix * smoothstep(0.0, bezel, d);
    float lens = profile(d, bezel);
    if (debugView == 4) lens = 0.0;
    vec2 bend = inward * lens * lensPhysical.x * bezel; // lensPhysical.x = displacement per px of band
    vec2 dispersion = debugView == 4 ? vec2(0.0) : inward * profile(d, max(0.5 * bezel, 1.0)) * lensPhysical.y;
    vec2 samplePx = sampleOffset + local + bend;
    vec3 color = vec3(backgroundAt(samplePx + dispersion).r, backgroundAt(samplePx).g,
                      backgroundAt(samplePx - dispersion).b);
    float light = dot(-inward, normalize(vec2(-0.45, -0.89)));
    float lip = lensPhysical.z;
    float rim = exp(-d / max(lip, 0.5));
    float inset = exp(-pow((d - 0.5 * lip) / max(0.45 * lip, 0.25), 2.0));
    if (debugView < 4) {
        color *= 1.0 - 0.08 * inset * (0.5 + 0.5 * max(-light, 0.0));
        color += vec3(0.94, 0.97, 1.0) * specular
               * (rim * (0.3 + 0.7 * max(light, 0.0)) + inset * 0.22 * max(light, 0.0));
    }
    color = mix(color, tint.rgb, tint.a);
    if (debugView == 1) color = vec3(clamp(0.5 - sdf / max(bezel * 2.0, 1.0), 0.0, 1.0));
    if (debugView == 2) color = vec3(0.5 + inward * 0.5, 0.5);
    if (debugView == 3) color = vec3(lens, step(d, bezel), rim);
    float a = coverage * opacity;
    fragColor = vec4(color * a, a);
}
