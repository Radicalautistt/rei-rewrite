#version 450

layout (location = 0) in vec2 uv;
layout (location = 0) out vec4 pixelColor;

layout (set = 0, binding = 0) uniform sampler2D image;

void main () {
  pixelColor = texture (image, uv);
}
