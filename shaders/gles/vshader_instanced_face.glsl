#version 300 es
layout (location = 0) in vec3 aPos;
layout (location = 1) in vec2 aTexCoord;
layout (location = 2) in mat4 instanceModel;
layout (location = 6) in float instanceFaceIndex;

uniform mat4 uVP;

out vec3 vWorldPos;
flat out int vFaceIndex;

void main()
{
    vec4 worldPos = instanceModel * vec4(aPos, 1.0);
    gl_Position = uVP * worldPos;
    vWorldPos = worldPos.xyz;
    vFaceIndex = int(instanceFaceIndex + 0.5);
}
