#version 410 core
// Radial vignette: color in the middle, fading to color2 at the edges.
uniform vec2 uResolution;
uniform vec3 uColor;
uniform vec3 uColor2;
out vec4 fragColor;

void main() {
  vec2 uv = gl_FragCoord.xy / uResolution;
  vec2 d = (uv - 0.5) * vec2(uResolution.x / uResolution.y, 1.0);
  fragColor = vec4(mix(uColor, uColor2, smoothstep(0.15, 0.95, length(d))), 1.0);
}
