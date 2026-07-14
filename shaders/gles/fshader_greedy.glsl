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
uniform float uAirFogEnabled;
uniform float uUnderwaterFogEnabled;
uniform float uUnderwaterFogStart;
uniform float uUnderwaterFogEnd;
uniform float uUnderwaterFogMinBlend;
uniform float uUnderwaterFogSubmerged;
uniform float uCameraColumnSurfaceY;
uniform float uCameraColumnFluidIndex;
uniform float uCameraColumnBottomBlockY;
uniform float uEnvFogMultiplier;
uniform float uEnvMinAmbient;
uniform float uEnvDayFactor;
uniform float uEnvNightFactor;
uniform float uEnvSkyLightScale;
uniform float uEnvLightDebug;
uniform float uEnvLightDebugMode;
uniform float uEnvPrecipIntensity;
uniform float uEnvWetness;
uniform vec2 uFluidSurfaceOrigin;
uniform vec2 uFluidSurfaceInvSize;
uniform sampler2D uFluidSurfaceYMap;
uniform sampler2D uFluidIndexMap;
uniform sampler2D uFluidBottomBlockMap;
uniform vec3 uUnderwaterFogColors[3];

uniform sampler2D uOpaqueDepthMap;
uniform float uOpaqueDepthGuard;
uniform vec2 uOpaqueDepthScreenSize;
uniform float uOpaqueDepthBias;

const int kCrossFaceIndex = 127;

float surfaceYAt(vec2 worldXZ) {
    vec2 uv = (worldXZ - uFluidSurfaceOrigin) * uFluidSurfaceInvSize;
    if (any(lessThan(uv, vec2(0.0))) || any(greaterThan(uv, vec2(1.0)))) {
        if (uUnderwaterFogEnabled > 0.5 && uCameraColumnSurfaceY < 1e8) {
            return uCameraColumnSurfaceY;
        }
        return 1e9;
    }
    float h = texture(uFluidSurfaceYMap, uv).r;
    if (h < -500.0) {
        if (uUnderwaterFogEnabled > 0.5 && uCameraColumnSurfaceY < 1e8) {
            return uCameraColumnSurfaceY;
        }
        return 1e9;
    }
    return h;
}

uint fluidIndexAt(vec2 worldXZ) {
    vec2 uv = (worldXZ - uFluidSurfaceOrigin) * uFluidSurfaceInvSize;
    if (any(lessThan(uv, vec2(0.0))) || any(greaterThan(uv, vec2(1.0)))) {
        if (uUnderwaterFogEnabled > 0.5 && uCameraColumnFluidIndex > 0.5) {
            return uint(floor(uCameraColumnFluidIndex + 0.5));
        }
        return 0u;
    }
    uint fi = uint(floor(texture(uFluidIndexMap, uv).r * 255.0 + 0.5));
    if (fi == 0u && uUnderwaterFogEnabled > 0.5 && uCameraColumnFluidIndex > 0.5) {
        float sy = texture(uFluidSurfaceYMap, uv).r;
        if (sy < -500.0) {
            return uint(floor(uCameraColumnFluidIndex + 0.5));
        }
    }
    return fi;
}

float fluidBottomBlockYAt(vec2 worldXZ) {
    vec2 uv = (worldXZ - uFluidSurfaceOrigin) * uFluidSurfaceInvSize;
    if (any(lessThan(uv, vec2(0.0))) || any(greaterThan(uv, vec2(1.0)))) {
        if (uUnderwaterFogEnabled > 0.5 && uCameraColumnBottomBlockY < 1e8) {
            return uCameraColumnBottomBlockY;
        }
        return 1e9;
    }
    float h = texture(uFluidBottomBlockMap, uv).r;
    if (h < -500.0) {
        if (uUnderwaterFogEnabled > 0.5 && uCameraColumnBottomBlockY < 1e8) {
            return uCameraColumnBottomBlockY;
        }
        return 1e9;
    }
    return h;
}

vec2 fluidColumnSampleXZ(vec2 worldXZ) {
    return vec2(floor(worldXZ.x + 0.5), floor(worldXZ.y + 0.5));
}

void fluidSurfaceContextAt(vec2 worldXZ, out float surface_y, out uint fluid_index,
                         out float bottom_block_y) {
    vec2 center = fluidColumnSampleXZ(worldXZ);
    surface_y = surfaceYAt(center);
    fluid_index = fluidIndexAt(center);
    bottom_block_y = fluidBottomBlockYAt(center);

    const vec2 kNeighborOffsets[4] = vec2[](
        vec2(1.0, 0.0), vec2(-1.0, 0.0), vec2(0.0, 1.0), vec2(0.0, -1.0));
    for (int i = 0; i < 4; ++i) {
        vec2 neighbor = center + kNeighborOffsets[i];
        float sy_nb = surfaceYAt(neighbor);
        uint fi_nb = fluidIndexAt(neighbor);
        if (fi_nb == 0u || sy_nb > 1e8) {
            continue;
        }
        if (fluid_index == 0u) {
            surface_y = sy_nb;
            fluid_index = fi_nb;
            bottom_block_y = fluidBottomBlockYAt(neighbor);
        } else if (sy_nb > surface_y) {
            surface_y = sy_nb;
            fluid_index = fi_nb;
            bottom_block_y = fluidBottomBlockYAt(neighbor);
        }
    }
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

float computeDistanceFogFactor(float dist, float fogStart, float fogEnd,
                               float minBlend, float density) {
    float fogRange = max(fogEnd - fogStart, 0.001);
    float fogFactor = clamp((dist - fogStart) / fogRange, 0.0, 1.0);
    fogFactor = pow(fogFactor, max(density * max(uEnvFogMultiplier, 0.05), 0.1));
    return max(fogFactor, minBlend);
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
    float nightAmbient = uEnvMinAmbient * (0.25 + 0.5 * uEnvNightFactor);
    float blockAmbientFloor = clamp(uEnvMinAmbient * 0.15, 0.02, 0.5);
    float skyLit = mix(nightAmbient, 1.0, sky01 * uEnvDayFactor * uEnvSkyLightScale);
    float blockLit = mix(blockAmbientFloor, 1.0, block01);
    float lit = max(skyLit, blockLit);
    FragColor.rgb *= lit;
    if (uEnvLightDebugMode > 0.5) {
        if (uEnvLightDebugMode < 1.5) {
            FragColor.rgb = vec3(sky01, block01, 0.0);
        } else if (uEnvLightDebugMode < 2.5) {
            FragColor.rgb = vec3(sky01);
        } else {
            FragColor.rgb = vec3(block01);
        }
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
    bool frag_underwater = false;
    vec3 underwater_fog_color = uFogColor;

    if (uUnderwaterFogEnabled > 0.5 && uFluidSurfaceInvSize.x > 0.0 &&
        vFaceIndex != kCrossFaceIndex) {
        float sy;
        float bottom_block_y;
        uint fi;
        fluidSurfaceContextAt(vWorldPos.xz, sy, fi, bottom_block_y);
        int block_index_y = blockIndexYFromFace(vWorldPos.y, vFaceIndex);
        int surface_block_y = int(round(sy - 0.5));
        int bottom_block_index_y = int(round(bottom_block_y));
        bool in_fluid_span = false;
        if (uUnderwaterFogSubmerged > 0.5) {
            in_fluid_span = true;
        } else if (bottom_block_index_y < 500 &&
                   block_index_y <= surface_block_y) {
            if (block_index_y + 1 >= bottom_block_index_y) {
                in_fluid_span = true;
            } else if (bottom_block_index_y == surface_block_y) {
                in_fluid_span = true;
            }
        }
        if (in_fluid_span && fi > 0u && sy < 1e8 && vWorldPos.y < sy) {
            frag_underwater = true;
            underwater_fog_color = uUnderwaterFogColors[int(fi)];
            float dist = length(vWorldPos - uCameraPos);
            float fog_factor = computeDistanceFogFactor(
                dist, uUnderwaterFogStart, uUnderwaterFogEnd,
                uUnderwaterFogMinBlend, 1.0);
            FragColor.rgb = mix(FragColor.rgb, underwater_fog_color, fog_factor);
        }
    }

    if (uFogEnabled > 0.5) {
        float dist = length(vWorldPos - uCameraPos);
        float fog_factor = computeDistanceFogFactor(
            dist, uFogStart, uFogEnd, uFogMinBlend, uFogDensity);
        FragColor.rgb = mix(FragColor.rgb, uFogColor, fog_factor);
    } else if (uAirFogEnabled > 0.5 && !frag_underwater) {
        float dist;
        if (uFogHorizontal > 0.5) {
            vec2 delta_xz = vWorldPos.xz - uCameraPos.xz;
            dist = length(delta_xz);
        } else {
            dist = length(vWorldPos - uCameraPos);
        }
        float fog_factor = computeDistanceFogFactor(
            dist, uFogStart, uFogEnd, uFogMinBlend, uFogDensity);
        FragColor.rgb = mix(FragColor.rgb, uFogColor, fog_factor);
    }
}

