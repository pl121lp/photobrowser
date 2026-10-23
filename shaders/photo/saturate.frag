// Photo effect 3: boosted colour saturation.
// No #version: spliced into tile.frag.
vec3 photoEffect(sampler2D tex, vec2 uv, vec3 c) {
  float luma = dot(c, vec3(0.2126, 0.7152, 0.0722));
  return clamp(mix(vec3(luma), c, 1.8), 0.0, 1.0);
}
