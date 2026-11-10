#version 330 core

layout (location = 0) in vec3 aPos;
layout (location = 1) in float aFaceIndex;
layout (location = 2) in vec2 aUV;
layout (location = 3) in vec3 aInstanceOffset;
layout (location = 4) in float aSkyLight;
layout (location = 5) in float aBlockLight;

out vec3 vWorldPos;
flat out vec3 vInstanceCell;
out vec2 vUV;
flat out int vFaceIndex;
out float vSkyLight;
out float vBlockLight;
out float vWetness;
flat out float vLightPreview;

uniform mat4 mvp_matrix;
uniform int uDecorShape;

uint decorMix(uint value)
{
    value ^= value >> 16;
    value *= 0x7feb352du;
    value ^= value >> 15;
    value *= 0x846ca68bu;
    return value ^ (value >> 16);
}

float decorRandom(uint value)
{
    return float(decorMix(value) & 0x00ffffffu) / 16777216.0;
}

void main()
{
    vec3 localPos = aPos;
    // Organic cross meshes share a GPU template. Vary each instance from its
    // integer world cell so neighboring plants do not repeat an identical
    // outline, while keeping runs reproducible and the bottom anchored.
    // Shape IDs: 1 tuft, 2 cluster, 3 reed, 4 flame, 5 pad, 6 mushroom.
    if (uDecorShape >= 1 && uDecorShape <= 6)
    {
        vec3 cell = floor(aInstanceOffset + vec3(0.5));
        uint seed = uint(int(cell.x)) * 0x8da6b343u
                  ^ uint(int(cell.y)) * 0xd8163841u
                  ^ uint(int(cell.z)) * 0xcb1ab31fu
                  ^ uint(uDecorShape) * 0x9e3779b9u;
        float widthX = 0.72 + decorRandom(seed ^ 0x68bc21ebu) * 0.56;
        float widthZ = 0.72 + decorRandom(seed ^ 0x02e5be93u) * 0.56;
        float heightScale = 0.78 + decorRandom(seed ^ 0x967a889bu) * 0.44;
        float yaw = decorRandom(seed ^ 0x3c6ef372u) * 6.28318530718;
        float leanAngle = decorRandom(seed ^ 0x4f1bbcdcu) * 6.28318530718;
        float leanAmount = (decorRandom(seed ^ 0x5f356495u) - 0.5) * 0.30;
        if (uDecorShape == 2) // flowers vary gently around their recognizable head
        {
            widthX = 0.84 + decorRandom(seed ^ 0x68bc21ebu) * 0.32;
            widthZ = 0.84 + decorRandom(seed ^ 0x02e5be93u) * 0.32;
            heightScale = 0.86 + decorRandom(seed ^ 0x967a889bu) * 0.28;
            leanAmount *= 0.65;
        }
        else if (uDecorShape == 3) // reeds keep their tall, narrow character
        {
            widthX = 0.88 + decorRandom(seed ^ 0x68bc21ebu) * 0.24;
            widthZ = 0.88 + decorRandom(seed ^ 0x02e5be93u) * 0.24;
            heightScale = 0.86 + decorRandom(seed ^ 0x967a889bu) * 0.30;
            leanAmount *= 0.65;
        }
        else if (uDecorShape == 4) // fire flickers in size, not in placement
        {
            widthX = 0.92 + decorRandom(seed ^ 0x68bc21ebu) * 0.16;
            widthZ = 0.92 + decorRandom(seed ^ 0x02e5be93u) * 0.16;
            heightScale = 0.86 + decorRandom(seed ^ 0x967a889bu) * 0.28;
            leanAmount *= 0.35;
        }
        else if (uDecorShape == 5) // lily pads stay close to the water plane
        {
            widthX = 0.92 + decorRandom(seed ^ 0x68bc21ebu) * 0.16;
            widthZ = 0.92 + decorRandom(seed ^ 0x02e5be93u) * 0.16;
            heightScale = 1.0;
            leanAmount = 0.0;
        }
        else if (uDecorShape == 6) // mushrooms vary gently, preserving their cap
        {
            widthX = 0.88 + decorRandom(seed ^ 0x68bc21ebu) * 0.24;
            widthZ = 0.88 + decorRandom(seed ^ 0x02e5be93u) * 0.24;
            heightScale = 0.88 + decorRandom(seed ^ 0x967a889bu) * 0.24;
            leanAmount *= 0.30;
        }
        float heightT = clamp(localPos.y + 0.5, 0.0, 1.25);
        vec2 footprint = vec2(localPos.x * widthX, localPos.z * widthZ);
        if (uDecorShape != 5) // preserve the notched lily-pad cardinal UV orientation
        {
            float c = cos(yaw);
            float s = sin(yaw);
            footprint = vec2(c * footprint.x - s * footprint.y,
                             s * footprint.x + c * footprint.y);
        }
        localPos.x = footprint.x;
        localPos.z = footprint.y;
        localPos.y = -0.5 + heightT * heightScale;
        localPos.x += cos(leanAngle) * leanAmount * heightT;
        localPos.z += sin(leanAngle) * leanAmount * heightT;
    }
    vec3 worldPos = localPos + aInstanceOffset;
    gl_Position = mvp_matrix * vec4(worldPos, 1.0);
    vWorldPos = worldPos;
    vInstanceCell = floor(aInstanceOffset + vec3(0.5));
    vUV = aUV;
    vFaceIndex = int(aFaceIndex + 0.5);
    vSkyLight = aSkyLight;
    vBlockLight = aBlockLight;
    vWetness = 0.0;
    vLightPreview = 0.0;
}
