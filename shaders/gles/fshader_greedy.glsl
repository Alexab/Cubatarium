#version 300 es
precision mediump float;
in vec3 vWorldPos;
in vec2 vUV;
flat in int vFaceIndex;
in float vSkyLight;
in float vBlockLight;
in float vWetness;

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
uniform float uEnvFogMultiplier;
uniform float uEnvMinAmbient;
uniform float uEnvDayFactor;
uniform float uEnvNightFactor;
uniform float uEnvLightDebug;
uniform float uEnvPrecipIntensity;
uniform float uBelowSurfaceFog;
uniform float uBelowSurfaceFogMin;
uniform float uBelowSurfaceFogScale;
uniform float uBelowSurfaceFogDepthMin;
uniform vec2 uFluidSurfaceOrigin;
uniform vec2 uFluidSurfaceInvSize;
uniform sampler2D uFluidSurfaceYMap;
uniform sampler2D uFluidIndexMap;
uniform sampler2D uFluidBottomBlockMap;
uniform vec3 uBelowSurfaceFogColors[3];

uniform sampler2D uOpaqueDepthMap;
uniform float uOpaqueDepthGuard;
uniform vec2 uOpaqueDepthScreenSize;
uniform float uOpaqueDepthBias;

const int kCrossFaceIndex = 127;

float surfaceYAt(vec2 worldXZ) {
    vec2 uv = (worldXZ - uFluidSurfaceOrigin) * uFluidSurfaceInvSize;
    if (any(lessThan(uv, vec2(0.0))) || any(greaterThan(uv, vec2(1.0)))) {
        return 1e9;
    }
    float h = texture(uFluidSurfaceYMap, uv).r;
    if (h < -500.0) {
        return 1e9;
    }
    return h;
}

uint fluidIndexAt(vec2 worldXZ) {
    vec2 uv = (worldXZ - uFluidSurfaceOrigin) * uFluidSurfaceInvSize;
    if (any(lessThan(uv, vec2(0.0))) || any(greaterThan(uv, vec2(1.0)))) {
        return 0u;
    }
    return uint(floor(texture(uFluidIndexMap, uv).r * 255.0 + 0.5));
}

float fluidBottomBlockYAt(vec2 worldXZ) {
    vec2 uv = (worldXZ - uFluidSurfaceOrigin) * uFluidSurfaceInvSize;
    if (any(lessThan(uv, vec2(0.0))) || any(greaterThan(uv, vec2(1.0)))) {
        return 1e9;
    }
    float h = texture(uFluidBottomBlockMap, uv).r;
    if (h < -500.0) {
        return 1e9;
    }
    return h;
}

vec2 fluidColumnSampleXZ(vec2 worldXZ) {
    return vec2(floor(worldXZ.x + 0.5), floor(worldXZ.y + 0.5));
}

int blockIndexYFromFace(float worldY, int faceIndex) {
    if (faceIndex == 4) {
        return int(floor(worldY - 0.5));
    }
    if (faceIndex == 5) {
        return int(floor(worldY + 0.5));
    }
    return int(floor(worldY + 0.5 - 1e-4));
}

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
    float sky01 = clamp(vSkyLight, 0.0, 1.0);
    float block01 = clamp(vBlockLight, 0.0, 1.0);
    if (vFaceIndex == kCrossFaceIndex && sky01 < 0.01 && block01 < 0.01) {
        sky01 = 1.0;
    }
    float nightAmbient = uEnvMinAmbient * (0.25 + 0.5 * uEnvNightFactor);
    float blockAmbientFloor = clamp(uEnvMinAmbient * 0.15, 0.02, 0.5);
    float skyLit = mix(nightAmbient, 1.0, sky01 * uEnvDayFactor);
    float blockLit = mix(blockAmbientFloor, 1.0, block01);
    float lit = max(skyLit, blockLit);
    FragColor.rgb *= lit;
    if (uEnvLightDebug > 0.5) {
        FragColor.rgb = vec3(sky01, block01, 0.0);
    }
    float precip = clamp(uEnvPrecipIntensity, 0.0, 1.0);
    if (precip > 0.001) {
        float gray = dot(FragColor.rgb, vec3(0.299, 0.587, 0.114));
        FragColor.rgb = mix(FragColor.rgb, vec3(gray, gray, gray * 1.05), precip * 0.12);
    }
    if (uAlphaCutout != 0 && FragColor.a < 0.1) {
        discard;
    }
    if (uGreedyShaderMode == kGreedyModeShellDepth && FragColor.a < uShellAlphaThreshold) {
        discard;
    }
    if (uGreedyShaderMode == kGreedyModeFuzzyOnly && FragColor.a >= uShellAlphaThreshold) {
        discard;
    }
    if (uOpaqueDepthGuard > 0.5 && uGreedyShaderMode != kGreedyModeShellDepth &&
        uOpaqueDepthScreenSize.x > 0.0) {
        vec2 depthUv = gl_FragCoord.xy / uOpaqueDepthScreenSize;
        float opaqueDepth = texture(uOpaqueDepthMap, depthUv).r;
        if (gl_FragCoord.z > opaqueDepth + uOpaqueDepthBias) {
            discard;
        }
    }
    if (uBelowSurfaceFog > 0.001 && uFluidSurfaceInvSize.x > 0.0 &&
        vFaceIndex != kCrossFaceIndex) {
        bool applyTint = true;
        if (uBelowSurfaceFogDepthMin > 0.0 && vFaceIndex != 4) {
            applyTint = false;
        }
        if (applyTint) {
            vec2 sampleXZ = fluidColumnSampleXZ(vWorldPos.xz);
            float sy = surfaceYAt(sampleXZ);
            float bottomBlockY = fluidBottomBlockYAt(sampleXZ);
            int blockIndexY = blockIndexYFromFace(vWorldPos.y, vFaceIndex);
            int surfaceBlockY = int(round(sy - 0.5));
            int bottomBlockIndexY = int(round(bottomBlockY));
            bool inFluidSpan = blockIndexY + 1 >= bottomBlockIndexY &&
                               blockIndexY < surfaceBlockY;
            float depthBelow = sy - vWorldPos.y;
            if (inFluidSpan && vWorldPos.y < sy &&
                depthBelow >= uBelowSurfaceFogDepthMin) {
                uint fi = fluidIndexAt(sampleXZ);
                if (fi > 0u) {
                    vec3 fogCol = uBelowSurfaceFogColors[int(fi)];
                    float factor = clamp(uBelowSurfaceFogMin + depthBelow * uBelowSurfaceFogScale,
                                         uBelowSurfaceFogMin, 1.0);
                    FragColor.rgb = mix(FragColor.rgb, fogCol, factor * uBelowSurfaceFog);
                }
            }
        }
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
        fogFactor = pow(fogFactor, max(uFogDensity * max(uEnvFogMultiplier, 0.05), 0.1));
        fogFactor = max(fogFactor, uFogMinBlend);
        FragColor.rgb = mix(FragColor.rgb, uFogColor, fogFactor);
    }
}

