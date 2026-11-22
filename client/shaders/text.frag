#version 450
// Texto: atlas de um canal (cobertura) vezes a cor do vértice.
layout(set = 0, binding = 0) uniform sampler2D tex;
layout(push_constant) uniform Draw {
  vec4 screen;
  vec4 params;
} draw;
layout(location = 0) in vec2 vUv;
layout(location = 1) in vec4 vColor;
layout(location = 0) out vec4 outColor;
void main() {
  float a = texture(tex, vUv).r;
  outColor = vec4(vColor.rgb, vColor.a * clamp(a * draw.params.x + draw.params.y, 0.0, 1.0));
}
