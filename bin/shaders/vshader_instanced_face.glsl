#version 330 core

layout (location = 0) in vec3 aPos;
layout (location = 1) in vec2 aTexCoord;
layout (location = 2) in mat4 instanceMVP;
layout (location = 6) in float instanceFaceIndex;
layout (location = 7) in vec2 instanceQuadSize;

out vec2 TexCoord;

void main()
{
    gl_Position = instanceMVP * vec4(aPos, 1.0);
    int face = int(instanceFaceIndex + 0.5);
    float cubeShift = 1.0 / 6.0;
    float u0 = float(face) * cubeShift;
    float u1 = float(face + 1) * cubeShift;
    vec2 tiled = fract(aTexCoord * max(instanceQuadSize, vec2(1.0)));

    // Match InitCubeBuffers atlas layout (6 faces in a row, sides have V flipped)
    if (face == 4) {
        TexCoord = vec2(mix(u0, u1, tiled.y), mix(0.0, 1.0, tiled.x));
    } else if (face == 5) {
        TexCoord = vec2(mix(u0, u1, tiled.x), mix(0.0, 1.0, tiled.y));
    } else {
        TexCoord = vec2(mix(u0, u1, tiled.x), mix(1.0, 0.0, tiled.y));
    }
}
