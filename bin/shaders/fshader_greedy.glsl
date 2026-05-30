#version 330 core

in vec3 vWorldPos;
flat in int vFaceIndex;

out vec4 FragColor;

uniform sampler2D texture0;

float blockTileCoord(float axis)
{
    return fract(axis + 0.5);
}

vec2 atlasUVFromWorldPos(int faceIndex, vec3 worldPos)
{
    const float kCubeShift = 1.0 / 6.0;
    float u0 = float(faceIndex) * kCubeShift;
    float u1 = float(faceIndex + 1) * kCubeShift;

    if (faceIndex == 0) {
        return vec2(mix(u0, u1, blockTileCoord(worldPos.x)),
                    mix(1.0, 0.0, blockTileCoord(worldPos.y)));
    }
    if (faceIndex == 1) {
        return vec2(mix(u0, u1, 1.0 - blockTileCoord(worldPos.z)),
                    mix(1.0, 0.0, blockTileCoord(worldPos.y)));
    }
    if (faceIndex == 2) {
        return vec2(mix(u0, u1, 1.0 - blockTileCoord(worldPos.x)),
                    mix(1.0, 0.0, blockTileCoord(worldPos.y)));
    }
    if (faceIndex == 3) {
        return vec2(mix(u0, u1, blockTileCoord(worldPos.z)),
                    mix(1.0, 0.0, blockTileCoord(worldPos.y)));
    }
    if (faceIndex == 4) {
        return vec2(mix(u0, u1, blockTileCoord(worldPos.x)),
                    mix(0.0, 1.0, 1.0 - blockTileCoord(worldPos.z)));
    }
    return vec2(mix(u0, u1, blockTileCoord(worldPos.x)),
                mix(0.0, 1.0, blockTileCoord(worldPos.z)));
}

void main()
{
    FragColor = texture(texture0, atlasUVFromWorldPos(vFaceIndex, vWorldPos));
}
