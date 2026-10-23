#version 410 core
in vec2 vUv;
in vec2 vUvPrev;
flat in vec3 vParams;
in vec2 vLocal;
flat in vec2 vHalf;

uniform float uCornerRadius;  // fraction of the tile's shorter side; 0 = square

uniform sampler2D uTex;
uniform sampler2D uTexPrev;

out vec4 fragColor;

void main() {
  // Both samples are unconditional so implicit derivatives (mip selection) stay valid.
  vec4 cur = texture(uTex, vUv);
  vec4 prev = texture(uTexPrev, vUvPrev);
  vec3 c = mix(prev.rgb, cur.rgb, vParams.x) * (1.0 - vParams.y);
  // Rounded rectangle: signed distance in px, one pixel of anti-aliasing at the edge.
  float r = uCornerRadius * 2.0 * min(vHalf.x, vHalf.y);
  vec2 q = abs(vLocal) - (vHalf - r);
  float d = length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - r;
  float coverage = clamp(0.5 - d, 0.0, 1.0);
  fragColor = vec4(c, vParams.z * coverage);
}
