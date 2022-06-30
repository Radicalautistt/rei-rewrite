#version 450

layout (location = 0) in vec2 position;
layout (location = 1) in vec2 uv;
layout (location = 2) in vec4 color;

layout (location = 0) out vec2 outUv;
layout (location = 1) out vec4 outColor;

layout (push_constant) uniform PushConstants {
  vec2 scale;
} pushConstants;

void main () {
  outUv = uv;
  outColor = color;
  gl_Position = vec4 (position * pushConstants.scale + vec2 (-1.f), 0.f, 1.f);
}
