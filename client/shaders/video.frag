#version 450
// Quadro do vídeo: Y, Cb e Cr em três texturas de um canal (MPEG-1, BT.601 faixa limitada) para RGB.
layout(set = 0, binding = 0) uniform sampler2D texY;
layout(set = 0, binding = 1) uniform sampler2D texCb;
layout(set = 0, binding = 2) uniform sampler2D texCr;
layout(push_constant) uniform Draw {
  vec4 screen;
  vec4 params;
} draw;
layout(location = 0) in vec2 vUv;
layout(location = 1) in vec4 vColor;
layout(location = 0) out vec4 outColor;
void main() {
  float y = (texture(texY, vUv).r - 0.0625) * 1.164;
  float cb = texture(texCb, vUv).r - 0.5;
  float cr = texture(texCr, vUv).r - 0.5;
  vec3 rgb = vec3(y + 1.596 * cr, y - 0.392 * cb - 0.813 * cr, y + 2.017 * cb);
  outColor = vec4(clamp(rgb, 0.0, 1.0) * vColor.rgb, vColor.a);
}
