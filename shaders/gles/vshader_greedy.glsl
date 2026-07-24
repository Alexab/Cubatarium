#version 300 es
layout (location = 0) in vec3 aPos;
layout (location = 1) in float aFaceIndex;
layout (location = 2) in vec2 aUV;
layout (location = 3) in float aSkyLight;
layout (location = 4) in float aBlockLight;
layout (location = 5) in float aWetness;

out vec3 vWorldPos;
out vec2 vUV;
flat out int vFaceIndex;
out float vSkyLight;
out float vBlockLight;
out float vWetness;

uniform mat4 mvp_matrix;

void main()
{
    gl_Position = mvp_matrix * vec4(aPos, 1.0);
    vWorldPos = aPos;
    vUV = aUV;
    vFaceIndex = int(aFaceIndex + 0.5);
    vSkyLight = aSkyLight;
    vBlockLight = aBlockLight;
    vWetness = aWetness;
}
