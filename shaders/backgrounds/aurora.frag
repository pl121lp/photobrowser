#version 410 core
// Northern lights: soft curtains of color drifting over a color2 sky.
uniform vec2 uResolution;
uniform float uTime;
uniform vec3 uColor;
uniform vec3 uColor2;
out vec4 fragColor;

void main() {
  vec2 uv = gl_FragCoord.xy / uResolution;
  float x = uv.x * (uResolution.x / uResolution.y);
  float t = uTime * 0.15;
  float curtain = 0.0;
  for (int i = 0; i < 3; ++i) {
    float fi = float(i);
    float center = 0.55 + 0.12 * sin(x * (1.3 + fi * 0.7) + t * (1.0 + fi * 0.4) + fi * 2.1);
    float band = exp(-pow((uv.y - center) * (7.0 + fi * 3.0), 2.0));
    float ripple = 0.6 + 0.4 * sin(x * (9.0 + fi * 5.0) - t * 3.0 + fi);
    curtain += band * ripple * (0.55 - fi * 0.12);
  }
  vec3 sky = mix(uColor2, uColor2 * 0.4, uv.y);
  fragColor = vec4(sky + uColor * curtain, 1.0);
}
