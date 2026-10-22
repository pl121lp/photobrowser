#version 410 core
in vec2 vUv;
in vec2 vUvPrev;
flat in vec2 vParams;

uniform sampler2D uTex;
uniform sampler2D uTexPrev;

out vec4 fragColor;

void main() {
  // Both samples are unconditional so implicit derivatives (mip selection) stay valid.
  vec4 cur = texture(uTex, vUv);
  vec4 prev = texture(uTexPrev, vUvPrev);
  vec3 c = mix(prev.rgb, cur.rgb, vParams.x) * (1.0 - vParams.y);
  fragColor = vec4(c, 1.0);
}
