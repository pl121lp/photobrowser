#version 410 core
in vec2 vUv;
flat in vec4 vColor;

uniform sampler2D uFont;  // single channel: glyph coverage

out vec4 fragColor;

void main() {
  fragColor = vec4(vColor.rgb, vColor.a * texture(uFont, vUv).r);
}
