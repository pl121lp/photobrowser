#version 410 core
// Map trip path: one instance per segment; 4 vertices as a triangle strip around it,
// reaching past both ends for the round caps.
layout(location = 0) in vec4 aEnds;    // a.x, a.y, b.x, b.y (px)
layout(location = 1) in vec4 aColorA;  // linear RGBA at a
layout(location = 2) in vec4 aColorB;  // linear RGBA at b
layout(location = 3) in float aWidth;  // px

uniform vec2 uViewport;

out vec2 vLocal;  // px: x along the segment from a, y across it
flat out float vLength;
flat out float vHalfWidth;
flat out vec4 vColorA;
flat out vec4 vColorB;

void main() {
  vec2 a = aEnds.xy, b = aEnds.zw;
  float len = length(b - a);
  vec2 dir = len > 1e-4 ? (b - a) / len : vec2(1.0, 0.0);
  vec2 nrm = vec2(-dir.y, dir.x);
  float r = 0.5 * aWidth + 1.0;
  vec2 corner = vec2(gl_VertexID & 1, gl_VertexID >> 1);
  vLocal = vec2(mix(-r, len + r, corner.x), mix(-r, r, corner.y));
  vec2 px = a + dir * vLocal.x + nrm * vLocal.y;
  vec2 ndc = px / uViewport * 2.0 - 1.0;
  gl_Position = vec4(ndc.x, -ndc.y, 0.0, 1.0);
  vLength = len;
  vHalfWidth = 0.5 * aWidth;
  vColorA = aColorA;
  vColorB = aColorB;
}
