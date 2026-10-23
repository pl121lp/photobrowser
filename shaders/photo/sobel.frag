// Photo effect 2: Sobel edge detection, edges white on black.
// No #version: spliced into tile.frag, which provides effectStep() and effectSample().
float sobelLuma(sampler2D tex, vec2 uv) { return dot(effectSample(tex, uv), vec3(0.2126, 0.7152, 0.0722)); }

vec3 photoEffect(sampler2D tex, vec2 uv, vec3 c) {
  vec2 d = effectStep(tex, uv);
  float tl = sobelLuma(tex, uv + d * vec2(-1, -1)), t = sobelLuma(tex, uv + d * vec2(0, -1));
  float tr = sobelLuma(tex, uv + d * vec2(1, -1)), l = sobelLuma(tex, uv + d * vec2(-1, 0));
  float r = sobelLuma(tex, uv + d * vec2(1, 0)), bl = sobelLuma(tex, uv + d * vec2(-1, 1));
  float b = sobelLuma(tex, uv + d * vec2(0, 1)), br = sobelLuma(tex, uv + d * vec2(1, 1));
  float gx = (tr + 2.0 * r + br) - (tl + 2.0 * l + bl);
  float gy = (bl + 2.0 * b + br) - (tl + 2.0 * t + tr);
  return vec3(clamp(length(vec2(gx, gy)), 0.0, 1.0));
}
