#version 300 es
precision mediump float;

layout (location = 0) in vec3 aPos;
layout (location = 1) in vec2 aTexCoord;

out vec2 vTexCoord;

uniform mat4 uMvp;

void main()
{
  gl_Position = uMvp * vec4(aPos, 1.0);
  vTexCoord = aTexCoord;
}
