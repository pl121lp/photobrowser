#version 410 core
// Drifting clouds of color over color2 (3 octaves of value noise).
uniform vec2 uResolution;
uniform float uTime;
uniform vec3 uColor;
uniform vec3 uColor2;
out vec4 fragColor;

float hash(vec2 p) { return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453); }

float noise(vec2 p) {
  vec2 i = floor(p), f = fract(p);
  vec2 u = f * f * (3.0 - 2.0 * f);
  return mix(mix(hash(i), hash(i + vec2(1, 0)), u.x), mix(hash(i + vec2(0, 1)), hash(i + vec2(1, 1)), u.x), u.y);
}

void main() {
  vec2 p = gl_FragCoord.xy / uResolution.y * 2.5;
  float t = uTime * 0.03;
  float v = 0.0, amp = 0.55;
  for (int i = 0; i < 3; ++i) {
    v += amp * noise(p + vec2(t * (1.0 + float(i)), -t * 0.7));
    p *= 2.1;
    amp *= 0.5;
  }
  fragColor = vec4(mix(uColor2, uColor, smoothstep(0.3, 0.9, v)), 1.0);
}
