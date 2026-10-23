// Photo effect 4: black & white, like film behind a light red filter (darker skies,
// brighter skin), with a gentle S-curve for contrast.
// No #version: spliced into tile.frag.
vec3 photoEffect(sampler2D tex, vec2 uv, vec3 c) {
  float y = dot(c, vec3(0.45, 0.45, 0.10));
  float g = pow(clamp(y, 0.0, 1.0), 1.0 / 2.2);  // tone curve in perceptual space
  g = mix(g, g * g * (3.0 - 2.0 * g), 0.35);
  return vec3(pow(g, 2.2));
}
