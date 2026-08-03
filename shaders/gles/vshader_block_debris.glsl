#version 300 es
precision mediump float;

layout (location = 0) in vec2 aCorner;
layout (location = 1) in vec3 aWorldPos;
layout (location = 2) in float aSize;
layout (location = 3) in vec4 aColor;

out vec4 vColor;
out vec2 vLocal;

uniform mat4 uViewProj;
uniform vec3 uCameraRight;
uniform vec3 uCameraUp;

void main()
{
  float size = max(aSize, 0.01);
  vec3 offset = uCameraRight * aCorner.x * size + uCameraUp * aCorner.y * size;
  gl_Position = uViewProj * vec4(aWorldPos + offset, 1.0);
  vColor = aColor;
  vLocal = aCorner;
}
