#version 410 core
// One instance per tile; 4 vertices as a triangle strip.
layout(location = 0) in vec4 aRect;    // screen px: x0, y0, x1, y1
layout(location = 1) in vec4 aUv;      // u0, v0, u1, v1 in uTex
layout(location = 2) in vec4 aUvPrev;  // same in uTexPrev
layout(location = 3) in vec2 aParams;  // fade (weight of uTex), dim

uniform vec2 uViewport;

out vec2 vUv;
out vec2 vUvPrev;
flat out vec2 vParams;

void main() {
  vec2 corner = vec2(gl_VertexID & 1, gl_VertexID >> 1);
  vec2 px = mix(aRect.xy, aRect.zw, corner);
  vec2 ndc = px / uViewport * 2.0 - 1.0;
  gl_Position = vec4(ndc.x, -ndc.y, 0.0, 1.0);
  vUv = mix(aUv.xy, aUv.zw, corner);
  vUvPrev = mix(aUvPrev.xy, aUvPrev.zw, corner);
  vParams = aParams;
}
