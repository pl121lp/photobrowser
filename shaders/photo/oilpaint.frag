// Photo effect 7: oil paint (Kuwahara filter). Each pixel takes the mean of whichever of
// the four quadrants around it is most uniform: flat brush-like patches, edges kept.
// 4 x 25 samples: the heaviest effect, fine for the one open photo.
// No #version: spliced into tile.frag, which provides effectStep() and effectSample().
vec3 photoEffect(sampler2D tex, vec2 uv, vec3 c) {
  const int R = 4;
  const float n = float((R + 1) * (R + 1));
  vec2 d = effectStep(tex, uv) * 1.5;  // brush size in screen pixels: about 6
  vec3 bestMean = c;
  float bestVar = 1e9;
  for (int q = 0; q < 4; ++q) {
    vec2 dir = vec2(q == 1 || q == 3 ? 1.0 : -1.0, q >= 2 ? 1.0 : -1.0);
    vec3 sum = vec3(0.0), sum2 = vec3(0.0);
    for (int j = 0; j <= R; ++j) {
      for (int i = 0; i <= R; ++i) {
        vec3 s = pow(effectSample(tex, uv + dir * vec2(i, j) * d), vec3(1.0 / 2.2));
        sum += s;
        sum2 += s * s;
      }
    }
    vec3 mean = sum / n;
    vec3 var = sum2 / n - mean * mean;
    float v = var.r + var.g + var.b;
    if (v < bestVar) {
      bestVar = v;
      bestMean = mean;
    }
  }
  return pow(bestMean, vec3(2.2));
}
