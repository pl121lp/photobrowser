#version 410 core
// Soft light falling from above: color under the light, color2 in the shadows.
uniform vec2 uResolution;
uniform vec3 uColor;
uniform vec3 uColor2;
out vec4 fragColor;

void main() {
  vec2 uv = gl_FragCoord.xy / uResolution;
  vec2 p = (uv - vec2(0.5, 1.15)) * vec2(uResolution.x / uResolution.y, 1.0);
  float light = exp(-dot(p, p) * 1.6);
  fragColor = vec4(mix(uColor2, uColor, light), 1.0);
}
