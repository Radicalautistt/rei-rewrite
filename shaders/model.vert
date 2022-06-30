#version 450

layout (location = 0) in vec3 position;
layout (location = 1) in vec3 normal;
layout (location = 2) in vec2 uv;

layout (location = 0) out vec2 out_uv;

layout (push_constant) uniform Mvp {
  mat4 data;
} mvp;

void main () {
  out_uv = uv;
  gl_Position = mvp.data * vec4 (position, 1.f);
}
