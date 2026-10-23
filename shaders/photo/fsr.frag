// Photo effect 9: FSR upscale. A single-pass port of AMD FidelityFX Super Resolution 1.0:
// EASU (edge-adaptive spatial upscaling, a 12-tap Lanczos-like filter steered by the local
// edge direction) followed by RCAS (robust contrast-adaptive sharpening).
//  - Photo enlarged on screen (zoomed past 1:1): EASU reconstructs each screen pixel from
//    the full-resolution texels instead of bilinear filtering. RCAS runs first, in texel
//    space, on EASU's four centre texels: their '+' rings are already among EASU's 12
//    taps, so sharpening costs no extra fetches (FSR proper sharpens the upscaled image in
//    a second pass; at several times enlargement neighbouring screen pixels are nearly
//    equal, so sharpening there achieves little and would need EASU five times per pixel).
//  - Not enlarged (fit to window or smaller): nothing to upscale; RCAS alone sharpens the
//    filtered image (like effect 1, but contrast-limited, so no halos or clipping).
// Works in perceptual space (gamma 2 via sqrt), as FSR expects.
// No #version: spliced into tile.frag, which provides effectStep(), effectSample(),
// effectBounds and vHalf.
//
// Based on ffx_fsr1.h, Copyright (c) 2021 Advanced Micro Devices, Inc. All rights reserved.
// Permission is hereby granted, free of charge, to any person obtaining a copy of this
// software and associated documentation files (the "Software"), to deal in the Software
// without restriction, including without limitation the rights to use, copy, modify, merge,
// publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons
// to whom the Software is furnished to do so, subject to the following conditions: The above
// copyright notice and this permission notice shall be included in all copies or substantial
// portions of the Software. THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
// EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
// FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
// HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF
// CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
// USE OR OTHER DEALINGS IN THE SOFTWARE.

const float kFsrSharpness = 0.87;  // RCAS: exp2(-0.2 stops); 1 = strongest

// One texel at level 0, clamped to the photo's texels, in perceptual space.
vec3 fsrTap(sampler2D tex, ivec2 p, ivec2 lo, ivec2 hi) {
  return sqrt(texelFetch(tex, clamp(p, lo, hi), 0).rgb);
}

float fsrLuma(vec3 c) { return c.b * 0.5 + c.r * 0.5 + c.g; }  // luma times 2, as in FSR

// Accumulates edge direction and length for one of the four bilinear corners, from its
// '+' neighbourhood (A above, B left, C centre, D right, E below), weighted by w.
void fsrEasuSet(inout vec2 dir, inout float len, float w, float lA, float lB, float lC, float lD, float lE) {
  float lenX = 1.0 / max(max(abs(lD - lC), abs(lC - lB)), 1e-8);
  float dirX = lD - lB;
  dir.x += dirX * w;
  lenX = clamp(abs(dirX) * lenX, 0.0, 1.0);
  len += lenX * lenX * w;
  float lenY = 1.0 / max(max(abs(lE - lC), abs(lC - lA)), 1e-8);
  float dirY = lE - lA;
  dir.y += dirY * w;
  lenY = clamp(abs(dirY) * lenY, 0.0, 1.0);
  len += lenY * lenY * w;
}

// Adds one tap at `off` (texels from the sample point) with the rotated, anisotropic,
// windowed Lanczos-2 approximation.
void fsrEasuTap(inout vec3 aC, inout float aW, vec2 off, vec2 dir, vec2 len, float lob, float clp, vec3 c) {
  vec2 v = vec2(off.x * dir.x + off.y * dir.y, off.x * -dir.y + off.y * dir.x) * len;
  float d2 = min(dot(v, v), clp);
  float wB = 2.0 / 5.0 * d2 - 1.0;
  float wA = lob * d2 - 1.0;
  wB *= wB;
  wA *= wA;
  wB = 25.0 / 16.0 * wB - (25.0 / 16.0 - 1.0);
  float w = wB * wA;
  aC += c * w;
  aW += w;
}

// RCAS on a centre pixel e and its '+' ring (b above, d left, f right, h below): the
// strongest sharpening that neither clips nor overshoots the ring's range. The limit is
// computed on luma rather than per channel (as in common FSR ports): a third of the
// divisions, nearly identical results.
vec3 fsrRcas(vec3 b, vec3 d, vec3 e, vec3 f, vec3 h) {
  float bL = fsrLuma(b) * 0.5, dL = fsrLuma(d) * 0.5, eL = fsrLuma(e) * 0.5, fL = fsrLuma(f) * 0.5, hL = fsrLuma(h) * 0.5;
  float mn4 = min(min(bL, dL), min(fL, hL)), mx4 = max(max(bL, dL), max(fL, hL));
  float hitMin = min(mn4, eL) / max(4.0 * mx4, 1e-5);
  float hitMax = (1.0 - max(mx4, eL)) / min(4.0 * mn4 - 4.0, -1e-5);
  float lobe = max(-(0.25 - 1.0 / 16.0), min(max(-hitMin, hitMax), 0.0)) * kFsrSharpness;
  return (lobe * (b + d + f + h) + e) / (4.0 * lobe + 1.0);
}

// EASU at `uv`. Taps around the sample point, f at floor(pp):
//     b c
//   e f g h
//   i j k l
//     n o
vec3 fsrEasu(sampler2D tex, vec2 uv, vec2 size, ivec2 lo, ivec2 hi) {
  vec2 pp = uv * size - 0.5;
  vec2 fp = floor(pp);
  pp -= fp;
  ivec2 o0 = ivec2(fp);
  vec3 b = fsrTap(tex, o0 + ivec2(0, -1), lo, hi), c = fsrTap(tex, o0 + ivec2(1, -1), lo, hi);
  vec3 e = fsrTap(tex, o0 + ivec2(-1, 0), lo, hi), f = fsrTap(tex, o0, lo, hi);
  vec3 g = fsrTap(tex, o0 + ivec2(1, 0), lo, hi), h = fsrTap(tex, o0 + ivec2(2, 0), lo, hi);
  vec3 i = fsrTap(tex, o0 + ivec2(-1, 1), lo, hi), j = fsrTap(tex, o0 + ivec2(0, 1), lo, hi);
  vec3 k = fsrTap(tex, o0 + ivec2(1, 1), lo, hi), l = fsrTap(tex, o0 + ivec2(2, 1), lo, hi);
  vec3 n = fsrTap(tex, o0 + ivec2(0, 2), lo, hi), o = fsrTap(tex, o0 + ivec2(1, 2), lo, hi);
  // Sharpen the four centre texels with their '+' rings (all among the taps).
  vec3 fs = fsrRcas(b, e, f, g, j), gs = fsrRcas(c, f, g, h, k);
  vec3 js = fsrRcas(f, i, j, k, n), ks = fsrRcas(g, j, k, l, o);
  f = fs;
  g = gs;
  j = js;
  k = ks;
  float bL = fsrLuma(b), cL = fsrLuma(c), eL = fsrLuma(e), fL = fsrLuma(f), gL = fsrLuma(g), hL = fsrLuma(h);
  float iL = fsrLuma(i), jL = fsrLuma(j), kL = fsrLuma(k), lL = fsrLuma(l), nL = fsrLuma(n), oL = fsrLuma(o);

  vec2 dir = vec2(0.0);
  float len = 0.0;
  fsrEasuSet(dir, len, (1.0 - pp.x) * (1.0 - pp.y), bL, eL, fL, gL, jL);
  fsrEasuSet(dir, len, pp.x * (1.0 - pp.y), cL, fL, gL, hL, kL);
  fsrEasuSet(dir, len, (1.0 - pp.x) * pp.y, fL, iL, jL, kL, nL);
  fsrEasuSet(dir, len, pp.x * pp.y, gL, jL, kL, lL, oL);

  // Normalize the direction (no edge: horizontal).
  float dirR = dot(dir, dir);
  bool zero = dirR < 1.0 / 32768.0;
  dir = zero ? vec2(1.0, 0.0) : dir * inversesqrt(dirR);
  // Edge amount {0..2} -> {0..1}, shaped with a square.
  len *= 0.5;
  len *= len;
  // Stretch the kernel along the edge (1 axis-aligned .. sqrt(2) diagonal), shrink across.
  float stretch = dot(dir, dir) / max(abs(dir.x), abs(dir.y));
  vec2 len2 = vec2(1.0 + (stretch - 1.0) * len, 1.0 - 0.5 * len);
  // Window from +/- sqrt(2) (flat) to slightly beyond 2 (edges); clip distance^2 at its end.
  float lob = 0.5 + ((1.0 / 4.0 - 0.04) - 0.5) * len;
  float clp = 1.0 / lob;

  vec3 aC = vec3(0.0);
  float aW = 0.0;
  fsrEasuTap(aC, aW, vec2(0, -1) - pp, dir, len2, lob, clp, b);
  fsrEasuTap(aC, aW, vec2(1, -1) - pp, dir, len2, lob, clp, c);
  fsrEasuTap(aC, aW, vec2(-1, 1) - pp, dir, len2, lob, clp, i);
  fsrEasuTap(aC, aW, vec2(0, 1) - pp, dir, len2, lob, clp, j);
  fsrEasuTap(aC, aW, vec2(0, 0) - pp, dir, len2, lob, clp, f);
  fsrEasuTap(aC, aW, vec2(-1, 0) - pp, dir, len2, lob, clp, e);
  fsrEasuTap(aC, aW, vec2(1, 1) - pp, dir, len2, lob, clp, k);
  fsrEasuTap(aC, aW, vec2(2, 1) - pp, dir, len2, lob, clp, l);
  fsrEasuTap(aC, aW, vec2(2, 0) - pp, dir, len2, lob, clp, h);
  fsrEasuTap(aC, aW, vec2(1, 0) - pp, dir, len2, lob, clp, g);
  fsrEasuTap(aC, aW, vec2(1, 2) - pp, dir, len2, lob, clp, o);
  fsrEasuTap(aC, aW, vec2(0, 2) - pp, dir, len2, lob, clp, n);
  // Deringing: stay within the 2x2 neighbourhood's range.
  vec3 mn4 = min(min(f, g), min(j, k)), mx4 = max(max(f, g), max(j, k));
  return clamp(aC / aW, mn4, mx4);
}

vec3 photoEffect(sampler2D tex, vec2 uv, vec3 c) {
  vec2 d = effectStep(tex, uv);  // derivatives before any branch
  vec2 size = vec2(textureSize(tex, 0));
  vec2 lo = min(effectBounds.xy, effectBounds.zw), hi = max(effectBounds.xy, effectBounds.zw);
  // Screen pixels per texel. Only flat inputs, so the branch is uniform across the tile.
  float scale = 2.0 * vHalf.x / max((hi.x - lo.x) * size.x, 1.0);
  if (scale <= 1.0) {
    vec3 s = fsrRcas(sqrt(effectSample(tex, uv - vec2(0.0, d.y))), sqrt(effectSample(tex, uv - vec2(d.x, 0.0))),
                     sqrt(c), sqrt(effectSample(tex, uv + vec2(d.x, 0.0))), sqrt(effectSample(tex, uv + vec2(0.0, d.y))));
    return s * s;
  }
  ivec2 tlo = ivec2(floor(lo * size + 1e-3)), thi = ivec2(ceil(hi * size - 1e-3)) - 1;
  vec3 s = fsrEasu(tex, uv, size, tlo, thi);
  return s * s;
}
