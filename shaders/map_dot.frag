#version 410 core
// Signed-distance dot: filled disc, white ring, and an optional expanding halo.
in vec2 vLocal;
flat in vec4 vDot;
flat in vec4 vColor;
flat in float vHalo;

out vec4 fragColor;

void main() {
  float d = length(vLocal);
  float r = vDot.z, ring = vDot.w;
  float disc = 1.0 - smoothstep(r - 0.75, r + 0.75, d);
  float dot = 1.0 - smoothstep(r + ring - 0.75, r + ring + 0.75, d);  // disc and ring
  vec3 rgb = mix(vec3(1.0), vColor.rgb, disc);
  float a = dot;
  if (vHalo > 0.0) {
    float band = 1.0 - smoothstep(1.0, 2.5, abs(d - mix(r, 3.0 * r, vHalo)));
    float h = 0.6 * (1.0 - vHalo) * band * (1.0 - a);
    rgb = mix(vColor.rgb, rgb, a / max(a + h, 1e-4));
    a += h;
  }
  if (a <= 0.002) discard;
  fragColor = vec4(rgb, a * vColor.a);
}
