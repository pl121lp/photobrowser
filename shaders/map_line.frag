#version 410 core
// Signed-distance capsule: an antialiased segment with round caps, its colour blended
// from one end to the other.
in vec2 vLocal;
flat in float vLength;
flat in float vHalfWidth;
flat in vec4 vColorA;
flat in vec4 vColorB;

out vec4 fragColor;

void main() {
  float along = clamp(vLocal.x, 0.0, vLength);
  float d = length(vLocal - vec2(along, 0.0));
  float a = 1.0 - smoothstep(vHalfWidth - 0.75, vHalfWidth + 0.75, d);
  if (a <= 0.002) discard;
  vec4 c = mix(vColorA, vColorB, vLength > 0.0 ? along / vLength : 0.0);
  fragColor = vec4(c.rgb, a * c.a);
}
