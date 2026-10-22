#version 410 core
// Stats overlay: one instance per quad; 4 vertices as a triangle strip.
layout(location = 0) in vec4 aRect;   // screen px: x0, y0, x1, y1
layout(location = 1) in vec4 aUv;     // font atlas
layout(location = 2) in vec4 aColor;  // linear RGBA

uniform vec2 uViewport;

out vec2 vUv;
flat out vec4 vColor;

void main() {
  vec2 corner = vec2(gl_VertexID & 1, gl_VertexID >> 1);
  vec2 ndc = mix(aRect.xy, aRect.zw, corner) / uViewport * 2.0 - 1.0;
  gl_Position = vec4(ndc.x, -ndc.y, 0.0, 1.0);
  vUv = mix(aUv.xy, aUv.zw, corner);
  vColor = aColor;
}
