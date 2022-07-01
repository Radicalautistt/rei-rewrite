#version 450

layout (location = 0) in vec2 uv;
layout (location = 0) out vec4 pixel_color;

layout (set = 0, binding = 0) uniform sampler2D albedo;

void main () {
  pixel_color = texture (albedo, uv);
}
