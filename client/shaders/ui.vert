#version 450
// Interface 2D: posição em pixels, UV e cor por vértice. screen = (2/L, 2/A, -1, -1) leva pixel para NDC (y para baixo).
layout(push_constant) uniform Draw {
  vec4 screen;
  vec4 params;
} draw;
layout(location = 0) in vec2 inPos;
layout(location = 1) in vec2 inUv;
layout(location = 2) in vec4 inColor;
layout(location = 0) out vec2 vUv;
layout(location = 1) out vec4 vColor;
void main() {
  gl_Position = vec4(inPos * draw.screen.xy + draw.screen.zw, 0.0, 1.0);
  vUv = inUv;
  vColor = inColor;
}
