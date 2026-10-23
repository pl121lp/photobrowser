// Photo effect 5: posterize, five tones per channel (screen-print look).
// No #version: spliced into tile.frag.
vec3 photoEffect(sampler2D tex, vec2 uv, vec3 c) {
  const float levels = 5.0;
  vec3 p = pow(clamp(c, 0.0, 1.0), vec3(1.0 / 2.2));  // even steps to the eye
  p = floor(p * levels + 0.5) / levels;
  return pow(p, vec3(2.2));
}
