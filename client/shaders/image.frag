#version 450
// Imagem (miniatura) com cantos arredondados: textura vezes a cor do vértice, recortada pela distância assinada.
layout(set = 0, binding = 0) uniform sampler2D tex;
layout(push_constant) uniform Draw {
  vec4 screen;
  vec4 params;
} draw;
layout(location = 0) in vec2 vUv;
layout(location = 1) in vec4 vColor;
layout(location = 0) out vec4 outColor;
void main() {
  vec2 size = draw.params.xy;
  float r = draw.params.z;
  vec2 p = (vUv - vec2(0.5)) * size;
  vec2 q = abs(p) - (size * 0.5 - vec2(r));
  float d = length(max(q, vec2(0.0))) + min(max(q.x, q.y), 0.0) - r;
  float inside = clamp(0.5 - d, 0.0, 1.0);
  vec4 c = texture(tex, vec2(0.5 + (vUv.x - 0.5) * draw.params.w, vUv.y)) * vColor;
  outColor = vec4(c.rgb, c.a * inside);
}
