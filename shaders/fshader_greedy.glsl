#version 330 core

in vec3 vWorldPos;
in vec2 vUV;
flat in int vFaceIndex;

out vec4 FragColor;

uniform sampler2D texture0;
uniform int uAnimFrame;
uniform int uAnimFrameCount;
uniform int uAlphaCutout;
// uGreedyShaderMode: 0 = color, 1 = shell depth (discard a < threshold), 2 = fuzzy (discard a >= threshold)
uniform int uGreedyShaderMode;
uniform float uShellAlphaThreshold;

const int kGreedyModeColor = 0;
const int kGreedyModeShellDepth = 1;
const int kGreedyModeFuzzyOnly = 2;
uniform vec3 uCameraPos;
uniform vec3 uFogColor;
uniform float uFogStart;
uniform float uFogEnd;
uniform float uFogMinBlend;
uniform float uFogEnabled;
uniform float uFogHorizontal;
uniform float uFogDensity;
uniform float uFluidSurfaceY;
uniform float uBelowSurfaceFog;
uniform vec3 uBelowSurfaceFogColor;

const int kCrossFaceIndex = 127;

float blockTileCoord(float axis)
{
    return fract(axis + 0.5);
}

vec2 atlasHalfTexelInset()
{
    ivec2 atlasSize = textureSize(texture0, 0);
    vec2 safeSize = vec2(max(atlasSize.x, 1), max(atlasSize.y, 1));
    return 0.5 / safeSize;
}

float insetMix(float a, float b, float t, float inset)
{
    float span = b - a;
    float dir = sign(span);
    float safeInset = min(inset, abs(span) * 0.25);
    return mix(a + dir * safeInset, b - dir * safeInset, t);
}

vec2 atlasUVFromWorldPos(int faceIndex, vec3 worldPos)
{
    const float kCubeShift = 1.0 / 6.0;
    float u0 = float(faceIndex) * kCubeShift;
    float u1 = float(faceIndex + 1) * kCubeShift;
    vec2 inset = atlasHalfTexelInset();

    if (faceIndex == 0) {
        return vec2(insetMix(u0, u1, blockTileCoord(worldPos.x), inset.x),
                    insetMix(1.0, 0.0, blockTileCoord(worldPos.y), inset.y));
    }
    if (faceIndex == 1) {
        return vec2(insetMix(u0, u1, 1.0 - blockTileCoord(worldPos.z), inset.x),
                    insetMix(1.0, 0.0, blockTileCoord(worldPos.y), inset.y));
    }
    if (faceIndex == 2) {
        return vec2(insetMix(u0, u1, 1.0 - blockTileCoord(worldPos.x), inset.x),
                    insetMix(1.0, 0.0, blockTileCoord(worldPos.y), inset.y));
    }
    if (faceIndex == 3) {
        return vec2(insetMix(u0, u1, blockTileCoord(worldPos.z), inset.x),
                    insetMix(1.0, 0.0, blockTileCoord(worldPos.y), inset.y));
    }
    if (faceIndex == 4) {
        return vec2(insetMix(u0, u1, blockTileCoord(worldPos.x), inset.x),
                    insetMix(0.0, 1.0, 1.0 - blockTileCoord(worldPos.z), inset.y));
    }
    return vec2(insetMix(u0, u1, blockTileCoord(worldPos.x), inset.x),
                insetMix(0.0, 1.0, blockTileCoord(worldPos.z), inset.y));
}

vec2 crossAtlasUV(vec2 meshUV)
{
    const float kCubeShift = 1.0 / 6.0;
    float u0 = 0.0;
    float u1 = kCubeShift;
    vec2 inset = atlasHalfTexelInset();
    return vec2(insetMix(u0, u1, meshUV.x, inset.x), insetMix(1.0, 0.0, meshUV.y, inset.y));
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
    if (uGreedyShaderMode == kGreedyModeShellDepth && FragColor.a < uShellAlphaThreshold) {
        discard;
    }
    if (uGreedyShaderMode == kGreedyModeFuzzyOnly && FragColor.a >= uShellAlphaThreshold) {
        discard;
    }
    if (uBelowSurfaceFog > 0.001 && vWorldPos.y < uFluidSurfaceY) {
        float depthBelow = uFluidSurfaceY - vWorldPos.y;
        float belowFactor = uBelowSurfaceFog * clamp(0.52 + depthBelow * 0.35, 0.52, 1.0);
        FragColor.rgb = mix(FragColor.rgb, uBelowSurfaceFogColor, belowFactor);
    }
    if (uFogEnabled > 0.5) {
        float dist;
        if (uFogHorizontal > 0.5) {
            vec2 delta_xz = vWorldPos.xz - uCameraPos.xz;
            dist = length(delta_xz);
        } else {
            dist = length(vWorldPos - uCameraPos);
        }
        float fogRange = max(uFogEnd - uFogStart, 0.001);
        float fogFactor = clamp((dist - uFogStart) / fogRange, 0.0, 1.0);
        fogFactor = pow(fogFactor, max(uFogDensity, 0.1));
        fogFactor = max(fogFactor, uFogMinBlend);
        FragColor.rgb = mix(FragColor.rgb, uFogColor, fogFactor);
    }
}
