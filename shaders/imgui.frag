#version 450

layout (location = 0) in vec2 uv;
layout (location = 1) in vec4 color;

layout (location = 0) out vec4 pixelColor;

layout (set = 0, binding = 0) uniform sampler2D font;

void main () {
  pixelColor = texture (font, uv) * color;
}
