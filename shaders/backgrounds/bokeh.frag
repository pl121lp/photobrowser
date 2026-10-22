#version 410 core
// Out-of-focus lights in color slowly rising over color2.
// Two layers of grid cells, at most one light per cell kept inside its cell, so each
// pixel evaluates just two lights (cheap enough for 4K on integrated GPUs).
uniform vec2 uResolution;
uniform float uTime;
uniform vec3 uColor;
uniform vec3 uColor2;
uniform float uDensity;  // light count multiplier (config: density)
out vec4 fragColor;

float hash(vec2 p) { return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453); }

void main() {
  vec2 uv = gl_FragCoord.xy / uResolution.y;
  vec3 c = mix(uColor2, uColor2 * 0.5, uv.y);
  for (int layer = 0; layer < 2; ++layer) {
    float fl = float(layer);
    float cells = 3.5 + fl * 2.5;                // cells per screen height
    float speed = 0.025 - fl * 0.008;            // screen heights per second
    vec2 p = uv * cells + vec2(fl * 7.3, -uTime * speed * cells);
    vec2 cell = floor(p);
    float h = hash(cell);
    if (h < 1.0 - 0.45 * uDensity) continue;
    float r = 0.12 + 0.2 * hash(cell + 1.9);
    vec2 center = vec2(r) + (1.0 - 2.0 * r) * vec2(hash(cell + 3.1), hash(cell + 7.7));
    float d = length(fract(p) - center);
    c += uColor * (1.0 - smoothstep(r * 0.85, r, d)) * (0.07 + 0.12 * fract(h * 13.0));
  }
  fragColor = vec4(c, 1.0);
}
