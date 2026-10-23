#version 410 core
in vec2 vUv;
in vec2 vUvPrev;
flat in vec3 vParams;
in vec2 vLocal;
flat in vec2 vHalf;
flat in vec4 vContent;

uniform float uCornerRadius;  // fraction of the tile's shorter side; 0 = square
uniform float uTransparentBars;  // 1 = letterbox bars transparent, 0 = black

uniform sampler2D uTex;
uniform sampler2D uTexPrev;

out vec4 fragColor;

// Neighbour offset for photo effects: one screen pixel, at least one texel of the level.
vec2 effectStep(sampler2D tex, vec2 uv) {
  return max(fwidth(uv), 1.0 / vec2(textureSize(tex, 0)));
}

// Photo effect (shaders/photo/*.frag, keys 1-3 on the open photo): the renderer inserts
// `#define PHOTO_EFFECT` and the effect's `vec3 photoEffect(sampler2D, vec2, vec3)` here.
//@photo_effect

void main() {
  // Both samples are unconditional so implicit derivatives (mip selection) stay valid.
  vec4 cur = texture(uTex, vUv);
  vec4 prev = texture(uTexPrev, vUvPrev);
#ifdef PHOTO_EFFECT
  cur.rgb = photoEffect(uTex, vUv, cur.rgb);
  prev.rgb = photoEffect(uTexPrev, vUvPrev, prev.rgb);
#endif
  vec3 c = mix(prev.rgb, cur.rgb, vParams.x) * (1.0 - vParams.y);
  // Letterbox: black outside the image area (a full-tile area costs nothing visible).
  // The photo itself gets the rounded corners too, sized from its own shorter side.
  vec2 ch = abs(vContent.zw - vContent.xy) * 0.5;
  float rc = uCornerRadius * 2.0 * min(ch.x, ch.y);
  vec2 qc = abs(vLocal - (vContent.xy + vContent.zw) * 0.5) - (ch - rc);
  float dc = length(max(qc, 0.0)) + min(max(qc.x, qc.y), 0.0) - rc;
  float inImage = clamp(0.5 - dc, 0.0, 1.0);
  c *= mix(inImage, 1.0, uTransparentBars);  // black bars ...
  float barAlpha = mix(1.0, inImage, uTransparentBars);  // ... or see-through ones
  // Rounded rectangle: signed distance in px, one pixel of anti-aliasing at the edge.
  float r = uCornerRadius * 2.0 * min(vHalf.x, vHalf.y);
  vec2 q = abs(vLocal) - (vHalf - r);
  float d = length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - r;
  float coverage = clamp(0.5 - d, 0.0, 1.0);
  fragColor = vec4(c, vParams.z * coverage * barAlpha);
}
