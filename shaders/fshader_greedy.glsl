#version 330 core

in vec3 vWorldPos;
in vec2 vUV;
flat in int vFaceIndex;

out vec4 FragColor;

uniform sampler2D texture0;
uniform int uAnimFrame;
uniform int uAnimFrameCount;
uniform int uAlphaCutout;
uniform int uTransparentSubPass;
uniform float uShellAlphaThreshold;
uniform vec3 uCameraPos;
uniform vec3 uFogColor;
uniform float uFogStart;
uniform float uFogEnd;
uniform float uFogMinBlend;
uniform float uFogEnabled;

const int kCrossFaceIndex = 127;

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

vec2 crossAtlasUV(vec2 meshUV)
{
    const float kCubeShift = 1.0 / 6.0;
    float u0 = 0.0;
    float u1 = kCubeShift;
    return vec2(mix(u0, u1, meshUV.x), mix(1.0, 0.0, meshUV.y));
}

void main()
{
    vec2 uv;
    if (vFaceIndex == kCrossFaceIndex) {
        uv = crossAtlasUV(vUV);
    } else {
        uv = atlasUVFromWorldPos(vFaceIndex, vWorldPos);
    }
    if (uAnimFrameCount > 1) {
        float frameH = 1.0 / float(uAnimFrameCount);
        uv.y = uv.y * frameH + float(uAnimFrame) * frameH;
    }
    FragColor = texture(texture0, uv);
    if (uAlphaCutout != 0 && FragColor.a < 0.1) {
        discard;
    }
    if (uTransparentSubPass == 1 && FragColor.a < uShellAlphaThreshold) {
        discard;
    }
    if (uTransparentSubPass == 2 && FragColor.a >= uShellAlphaThreshold) {
        discard;
    }
    if (uFogEnabled > 0.5) {
        float dist = length(vWorldPos - uCameraPos);
        float fogRange = max(uFogEnd - uFogStart, 0.001);
        float fogFactor = clamp((dist - uFogStart) / fogRange, 0.0, 1.0);
        fogFactor = max(fogFactor, uFogMinBlend);
        FragColor.rgb = mix(FragColor.rgb, uFogColor, fogFactor);
    }
}
