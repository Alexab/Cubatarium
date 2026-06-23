#version 300 es
layout (location = 0) in vec3 aPos;
layout (location = 1) in float aFaceIndex;
layout (location = 2) in vec2 aUV;

out vec3 vWorldPos;
out vec2 vUV;
flat out int vFaceIndex;

uniform mat4 mvp_matrix;

void main()
{
    gl_Position = mvp_matrix * vec4(aPos, 1.0);
    vWorldPos = aPos;
    vUV = aUV;
    vFaceIndex = int(aFaceIndex + 0.5);
}
