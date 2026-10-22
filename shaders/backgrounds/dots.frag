#version 410 core
// Fine dot grid in color2 over color, like a light table.
uniform vec2 uResolution;
uniform vec3 uColor;
uniform vec3 uColor2;
out vec4 fragColor;

void main() {
  float spacing = uResolution.y / 70.0;
  vec2 cell = fract(gl_FragCoord.xy / spacing) - 0.5;
  float dot_ = 1.0 - smoothstep(0.08, 0.14, length(cell));
  fragColor = vec4(mix(uColor, uColor2, dot_ * 0.6), 1.0);
}
