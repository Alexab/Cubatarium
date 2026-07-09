#version 330 core

layout (location = 0) in vec3 aPos;
layout (location = 1) in float aFaceIndex;
layout (location = 2) in vec2 aUV;
layout (location = 3) in vec3 aInstanceOffset;

out vec3 vWorldPos;
out vec2 vUV;
flat out int vFaceIndex;
out float vLight;
out float vWetness;

uniform mat4 mvp_matrix;

void main()
{
    vec3 worldPos = aPos + aInstanceOffset;
    gl_Position = mvp_matrix * vec4(worldPos, 1.0);
    vWorldPos = worldPos;
    vUV = aUV;
    vFaceIndex = int(aFaceIndex + 0.5);
    vLight = 1.0;
    vWetness = 0.0;
}
