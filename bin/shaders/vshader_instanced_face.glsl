#version 330 core

layout (location = 0) in vec3 aPos;
layout (location = 1) in vec2 aTexCoord;
layout (location = 2) in mat4 instanceModel;
layout (location = 6) in float instanceFaceIndex;

uniform mat4 uVP;

out vec2 TexCoord;

float voxelTile(float w)
{
    return w - floor(w - 0.5) - 0.5;
}

vec2 atlasUVFromWorldPos(int face, vec3 wp)
{
    float cubeShift = 1.0 / 6.0;
    float u0 = float(face) * cubeShift;
    float u1 = float(face + 1) * cubeShift;

    float tx = voxelTile(wp.x);
    float ty = voxelTile(wp.y);
    float tz = voxelTile(wp.z);

    if (face == 0) {
        return vec2(mix(u0, u1, tx), mix(1.0, 0.0, ty));
    }
    if (face == 1) {
        return vec2(mix(u0, u1, 1.0 - tz), mix(1.0, 0.0, ty));
    }
    if (face == 2) {
        return vec2(mix(u0, u1, 1.0 - tx), mix(1.0, 0.0, ty));
    }
    if (face == 3) {
        return vec2(mix(u0, u1, tz), mix(1.0, 0.0, ty));
    }
    if (face == 4) {
        return vec2(mix(u0, u1, tx), mix(0.0, 1.0, 1.0 - tz));
    }
    return vec2(mix(u0, u1, tx), mix(0.0, 1.0, tz));
}

void main()
{
    vec4 worldPos = instanceModel * vec4(aPos, 1.0);
    gl_Position = uVP * worldPos;
    int face = int(instanceFaceIndex + 0.5);
    TexCoord = atlasUVFromWorldPos(face, worldPos.xyz);
}
