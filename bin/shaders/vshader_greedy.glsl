#version 330 core

layout (location = 0) in vec3 aPos;
layout (location = 1) in float aFaceIndex;

out vec3 vWorldPos;
flat out int vFaceIndex;

uniform mat4 mvp_matrix;

void main()
{
    gl_Position = mvp_matrix * vec4(aPos, 1.0);
    vWorldPos = aPos;
    vFaceIndex = int(aFaceIndex + 0.5);
}
