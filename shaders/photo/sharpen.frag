// Photo effect 1: sharpen (unsharp mask against the four direct neighbours).
// No #version: spliced into tile.frag, which provides effectStep().
vec3 photoEffect(sampler2D tex, vec2 uv, vec3 c) {
  vec2 d = effectStep(tex, uv);
  vec3 blur = (texture(tex, uv + vec2(d.x, 0.0)).rgb + texture(tex, uv - vec2(d.x, 0.0)).rgb +
               texture(tex, uv + vec2(0.0, d.y)).rgb + texture(tex, uv - vec2(0.0, d.y)).rgb) * 0.25;
  return clamp(c + 1.5 * (c - blur), 0.0, 1.0);
}
