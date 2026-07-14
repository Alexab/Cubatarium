#version 300 es
precision mediump float;

layout (location = 0) in vec2 aCorner;
layout (location = 1) in vec3 aWorldPos;
layout (location = 2) in float aKind;
layout (location = 3) in float aSize;

out float vKind;
out float vAlpha;
out vec2 vTexCoord;

uniform mat4 uViewProj;
uniform vec3 uCameraRight;
uniform vec3 uCameraUp;
uniform float uDayFactor;

void main()
{
  float size = max(aSize, 0.02);
  vec3 offset = uCameraRight * aCorner.x * size + uCameraUp * aCorner.y * size;
  if (aKind > 1.5)
  {
    offset.y += aCorner.x * size * 0.35;
  }
  vec3 world_pos = aWorldPos + offset;
  gl_Position = uViewProj * vec4(world_pos, 1.0);
  vKind = aKind;
  vTexCoord = aCorner * 0.5 + 0.5;
  vAlpha = 0.35 + 0.45 * clamp(uDayFactor, 0.0, 1.0);
}
