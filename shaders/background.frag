#version 410 core
// Built-in backgrounds. uMode 1 = vertical gradient, 2 = vignette.
uniform vec2 uResolution;
uniform vec3 uColor;
uniform vec3 uColor2;
uniform int uMode;

out vec4 fragColor;

void main() {
  vec2 uv = gl_FragCoord.xy / uResolution;
  vec3 c;
  if (uMode == 1) {
    c = mix(uColor2, uColor, uv.y);  // gl_FragCoord.y is 0 at the bottom
  } else {
    vec2 d = (uv - 0.5) * vec2(uResolution.x / uResolution.y, 1.0);
    c = mix(uColor, uColor2, smoothstep(0.15, 0.95, length(d)));
  }
  fragColor = vec4(c, 1.0);
}
