#version 450
// Desfoque só nos dois controles. Lê os planos que já estão na GPU, sem copiar o framebuffer.
layout(set = 0, binding = 0) uniform sampler2D texY;
layout(set = 0, binding = 1) uniform sampler2D texCb;
layout(set = 0, binding = 2) uniform sampler2D texCr;
layout(push_constant) uniform Draw { vec4 screen; vec4 params; } draw;
layout(location = 0) in vec2 vUv;
layout(location = 1) in vec4 vColor;
layout(location = 0) out vec4 outColor;

vec3 sampleVideo(vec2 uv) {
  return vec3(texture(texY, uv).r, texture(texCb, uv).r, texture(texCr, uv).r);
}

void main() {
  vec2 size = draw.params.xy;
  vec2 uv = vColor.xy, offset = vColor.zw;
  vec3 planes = (sampleVideo(uv) * 2.0 + sampleVideo(uv + offset) + sampleVideo(uv - offset)
                + sampleVideo(uv + vec2(offset.x, -offset.y)) + sampleVideo(uv + vec2(-offset.x, offset.y))) / 6.0;
  float full = draw.params.z, hd = draw.params.w;
  float y = (planes.x - (1.0 - full) * (16.0 / 255.0)) * mix(255.0 / 219.0, 1.0, full);
  vec2 chroma = (planes.yz - vec2(128.0 / 255.0)) * mix(255.0 / 224.0, 1.0, full);
  vec3 rgb = vec3(y + mix(1.402, 1.5748, hd) * chroma.y,
                  y - dot(mix(vec2(.344136, .714136), vec2(.187324, .468124), hd), chroma),
                  y + mix(1.772, 1.8556, hd) * chroma.x);
  // Escurecimento da ilha (0xa0) sobre o degradê do vídeo, entre y=640 e 692.
  float light = (1.0 - 160.0 / 255.0) * (1.0 - 0.87 * (220.0 + vUv.y * size.y) / 300.0);
  float r = size.y * 0.5;
  vec2 q = abs((vUv - 0.5) * size) - (size * 0.5 - vec2(r));
  float mask = clamp(0.5 + r - length(max(q, vec2(0.0))) - min(max(q.x, q.y), 0.0), 0.0, 1.0);
  outColor = vec4(clamp(rgb, 0.0, 1.0) * light, mask);
}
