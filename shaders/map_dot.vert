#version 410 core
// Map dots: one instance per dot; 4 vertices as a triangle strip around it.
layout(location = 0) in vec4 aDot;    // centre x, y (px), radius, ring width (px)
layout(location = 1) in vec4 aColor;  // linear RGBA of the disc
layout(location = 2) in float aHalo;  // 0 = none, else pulse phase 0..1

uniform vec2 uViewport;

out vec2 vLocal;  // px from the centre
flat out vec4 vDot;
flat out vec4 vColor;
flat out float vHalo;

void main() {
  vec2 corner = vec2(gl_VertexID & 1, gl_VertexID >> 1) * 2.0 - 1.0;
  float extent = aHalo > 0.0 ? 3.0 * aDot.z + 2.0 : aDot.z + aDot.w + 1.5;
  vec2 px = aDot.xy + corner * extent;
  vec2 ndc = px / uViewport * 2.0 - 1.0;
  gl_Position = vec4(ndc.x, -ndc.y, 0.0, 1.0);
  vLocal = corner * extent;
  vDot = aDot;
  vColor = aColor;
  vHalo = aHalo;
}
