// Photo effect 6: pencil sketch, dark Sobel strokes and light shading on warm paper.
// No #version: spliced into tile.frag, which provides effectStep() and effectSample().
float sketchTone(sampler2D tex, vec2 uv) {
  return pow(dot(effectSample(tex, uv), vec3(0.2126, 0.7152, 0.0722)), 1.0 / 2.2);
}

vec3 photoEffect(sampler2D tex, vec2 uv, vec3 c) {
  vec2 d = effectStep(tex, uv) * 1.5;  // slightly wider strokes than one pixel
  float tl = sketchTone(tex, uv + d * vec2(-1, -1)), t = sketchTone(tex, uv + d * vec2(0, -1));
  float tr = sketchTone(tex, uv + d * vec2(1, -1)), l = sketchTone(tex, uv + d * vec2(-1, 0));
  float r = sketchTone(tex, uv + d * vec2(1, 0)), bl = sketchTone(tex, uv + d * vec2(-1, 1));
  float b = sketchTone(tex, uv + d * vec2(0, 1)), br = sketchTone(tex, uv + d * vec2(1, 1));
  float gx = (tr + 2.0 * r + br) - (tl + 2.0 * l + bl);
  float gy = (bl + 2.0 * b + br) - (tl + 2.0 * t + tr);
  float stroke = smoothstep(0.08, 0.5, length(vec2(gx, gy)));
  float shade = mix(0.78, 1.0, smoothstep(0.1, 0.7, sketchTone(tex, uv)));  // soft graphite in the darks
  vec3 paper = vec3(0.96, 0.94, 0.89);
  return pow(paper * shade * (1.0 - 0.85 * stroke), vec3(2.2));
}
