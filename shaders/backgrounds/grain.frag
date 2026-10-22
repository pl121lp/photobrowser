#version 410 core
// Film grain: color with fine static noise and a gentle falloff towards color2.
uniform vec2 uResolution;
uniform vec3 uColor;
uniform vec3 uColor2;
out vec4 fragColor;

float hash(vec2 p) { return fract(sin(dot(p, vec2(12.9898, 78.233))) * 43758.5453); }

void main() {
  vec2 uv = gl_FragCoord.xy / uResolution;
  vec2 d = (uv - 0.5) * vec2(uResolution.x / uResolution.y, 1.0);
  vec3 base = mix(uColor, uColor2, smoothstep(0.3, 1.2, length(d)));
  float n = hash(floor(gl_FragCoord.xy)) - 0.5;
  fragColor = vec4(max(base + n * 0.02, 0.0), 1.0);
}
