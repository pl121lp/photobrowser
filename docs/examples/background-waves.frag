#version 410 core
// Example animated background for PhotoBrowse: [background] mode = shader, shader = <this file>.
uniform vec2 uResolution;
uniform float uTime;
uniform vec3 uColor;
uniform vec3 uColor2;
out vec4 fragColor;
void main() {
  vec2 uv = gl_FragCoord.xy / uResolution;
  float w = 0.5 + 0.5 * sin(uv.x * 12.0 + uTime) * cos(uv.y * 9.0 - uTime * 0.7);
  fragColor = vec4(mix(uColor2, uColor, w), 1.0);
}
