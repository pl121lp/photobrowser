#version 410 core
// Vertical gradient: color at the top, color2 at the bottom.
uniform vec2 uResolution;
uniform vec3 uColor;
uniform vec3 uColor2;
out vec4 fragColor;

void main() {
  float y = gl_FragCoord.y / uResolution.y;  // 0 at the bottom
  fragColor = vec4(mix(uColor2, uColor, y), 1.0);
}
