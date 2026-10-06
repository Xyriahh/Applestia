#version 300 es
precision highp float;
uniform sampler2D tex;
uniform vec2 sampleSize, storageSize;
uniform float radius;
in vec2 v_texcoord;
layout(location = 0) out vec4 fragColor;
void main() {
    vec2 p = v_texcoord * sampleSize;
    vec4 sum = vec4(0.0);
    for (int y = -1; y <= 1; ++y) for (int x = -1; x <= 1; ++x) {
        float weight = (x == 0 ? 2.0 : 1.0) * (y == 0 ? 2.0 : 1.0);
        vec2 uv = clamp(p + vec2(float(x), float(y)) * radius, vec2(0.5), sampleSize - 0.5) / storageSize;
        sum += texture(tex, uv) * weight;
    }
    fragColor = sum / 16.0;
}
