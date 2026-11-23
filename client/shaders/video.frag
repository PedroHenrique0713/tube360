#version 450
// Quadro do vídeo: Y, Cb e Cr em três texturas de um canal para RGB.
layout(set = 0, binding = 0) uniform sampler2D texY;
layout(set = 0, binding = 1) uniform sampler2D texCb;
layout(set = 0, binding = 2) uniform sampler2D texCr;
layout(push_constant) uniform Draw {
  vec4 screen;
  vec4 params;
  vec4 filterStep;
} draw;
layout(location = 0) in vec2 vUv;
layout(location = 1) in vec4 vColor;
layout(location = 0) out vec4 outColor;
void main() {
  float luma;
  vec2 offset = draw.filterStep.xy;
  if (draw.filterStep.x > 0.0) {
    luma = (texture(texY, vUv - offset).r + texture(texY, vUv + offset).r +
            texture(texY, vUv + vec2(offset.x, -offset.y)).r + texture(texY, vUv + vec2(-offset.x, offset.y)).r) * .25;
  } else luma = texture(texY, vUv).r;
  float y = (luma - draw.params.w) * draw.params.z;
  float cb = texture(texCb, vUv).r - 128.0/255.0;
  float cr = texture(texCr, vUv).r - 128.0/255.0;
  vec3 rgb = vec3(y + draw.params.x * cr, y - vColor.r * cb - vColor.g * cr, y + draw.params.y * cb);
  outColor = vec4(clamp(rgb, 0.0, 1.0), vColor.a);
}
