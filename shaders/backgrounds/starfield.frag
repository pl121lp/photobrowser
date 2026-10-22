#version 410 core
// Twinkling stars in color drifting slowly across a color2 night sky.
uniform vec2 uResolution;
uniform float uTime;
uniform vec3 uColor;
uniform vec3 uColor2;
out vec4 fragColor;

float hash(vec2 p) { return fract(sin(dot(p, vec2(41.3, 289.1))) * 43758.5453); }

void main() {
  vec3 c = uColor2;
  for (int layer = 0; layer < 3; ++layer) {
    float fl = float(layer);
    float cellSize = uResolution.y / (18.0 + fl * 14.0);
    vec2 p = gl_FragCoord.xy / cellSize + vec2(uTime * (0.05 + fl * 0.04), fl * 13.0);
    vec2 cell = floor(p);
    float h = hash(cell);
    if (h > 0.7) {
      vec2 star = vec2(hash(cell + 1.7), hash(cell + 4.3)) * 0.8 + 0.1;
      float d = length(fract(p) - star) * cellSize;
      float twinkle = 0.6 + 0.4 * sin(uTime * (1.0 + h * 3.0) + h * 40.0);
      c += uColor * (1.0 - smoothstep(0.0, 1.6 + fl * 0.4, d)) * twinkle * (0.9 - fl * 0.25);
    }
  }
  fragColor = vec4(c, 1.0);
}
