#version 450
// Retângulo arredondado por distância assinada (subconjunto do xost: sem if nem bit).
// params: x, y = tamanho do quad em px; z = raio; w = esfumado em px. Com esfumado f > 1 o quad vem f - 1 px maior de
// cada lado que a forma, e a opacidade cai até zero antes da borda do quad (sem borda dura).
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
  float feather = max(draw.params.w, 1.0);
  vec2 halfShape = size * 0.5 - vec2(feather - 1.0);
  vec2 p = (vUv - vec2(0.5)) * size;
  vec2 q = abs(p) - (halfShape - vec2(r));
  float d = length(max(q, vec2(0.0))) + min(max(q.x, q.y), 0.0) - r;
  float inside = clamp(0.5 - d / feather, 0.0, 1.0);
  outColor = vec4(vColor.rgb, vColor.a * inside);
}
