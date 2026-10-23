#version 410 core
in vec2 vUv;
in vec2 vUvPrev;
flat in vec3 vParams;
in vec2 vLocal;
flat in vec2 vHalf;
flat in vec4 vContent;
flat in vec4 vUvBounds, vUvPrevBounds;

uniform float uCornerRadius;  // fraction of the tile's shorter side; 0 = square
uniform float uTransparentBars;  // 1 = letterbox bars transparent, 0 = black

uniform sampler2D uTex;
uniform sampler2D uTexPrev;

out vec4 fragColor;

// Helpers for photo effects. effectBounds: the photo's rect in the texture being filtered,
// set before each photoEffect() call.
vec4 effectBounds;

// Neighbour offset: one screen pixel, at least one texel of the level.
vec2 effectStep(sampler2D tex, vec2 uv) {
  return max(fwidth(uv), 1.0 / vec2(textureSize(tex, 0)));
}

// texture(), clamped to the photo so neighbours never come from the next slot of the page.
vec3 effectSample(sampler2D tex, vec2 uv) {
  vec2 inset = 0.5 / vec2(textureSize(tex, 0));
  vec2 lo = min(effectBounds.xy, effectBounds.zw) + inset, hi = max(effectBounds.xy, effectBounds.zw) - inset;
  return texture(tex, clamp(uv, lo, hi)).rgb;
}

// Position in the drawn photo: -1..1 from left to right and top to bottom.
vec2 effectPos() { return vLocal / vHalf; }

// Photo effect (shaders/photo/*.frag, number keys on the open photo): the renderer inserts
// `#define PHOTO_EFFECT` and the effect's `vec3 photoEffect(sampler2D tex, vec2 uv, vec3 c)`
// here; `c` is the plain sample at `uv`.
//@photo_effect

void main() {
  // Both samples are unconditional so implicit derivatives (mip selection) stay valid.
  vec4 cur = texture(uTex, vUv);
  vec4 prev = texture(uTexPrev, vUvPrev);
#ifdef PHOTO_EFFECT
  effectBounds = vUvBounds;
  cur.rgb = photoEffect(uTex, vUv, cur.rgb);
  if (vParams.x < 1.0) {  // flat, so the branch is uniform per tile: derivatives stay valid
    effectBounds = vUvPrevBounds;
    prev.rgb = photoEffect(uTexPrev, vUvPrev, prev.rgb);
  }
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
