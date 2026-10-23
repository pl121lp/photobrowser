// Photo effect 8: tilt-shift miniature. A sharp band across the middle of the photo,
// increasingly blurred above and below, with a little extra colour and contrast.
// No #version: spliced into tile.frag, which provides effectStep(), effectSample() and
// effectPos().
vec3 photoEffect(sampler2D tex, vec2 uv, vec3 c) {
  float blur = smoothstep(0.15, 0.85, abs(effectPos().y));  // 0 in the band, 1 at the edges
  vec2 radius = effectStep(tex, uv) * 14.0 * blur;          // up to ~14 screen px
  // Vogel (golden-angle) disc: even coverage with few samples.
  const int N = 24;
  vec3 sum = c;
  for (int i = 0; i < N; ++i) {
    float a = float(i) * 2.39996323;
    float r = sqrt((float(i) + 0.5) / float(N));
    sum += effectSample(tex, uv + vec2(cos(a), sin(a)) * r * radius);
  }
  vec3 o = sum / float(N + 1);
  float y = dot(o, vec3(0.2126, 0.7152, 0.0722));
  o = mix(vec3(y), o, 1.3);                          // toy-like saturation
  o = mix(o, o * o * (3.0 - 2.0 * o), 0.25);         // and a touch of contrast
  return clamp(o, 0.0, 1.0);
}
