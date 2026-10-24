#version 300 es
precision mediump float;
in vec3 vWorldPos;
flat in vec3 vInstanceCell;
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
uniform float uTextureVariation;
uniform int uMaterialSeed;
uniform float uMaterialTimePhase;
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
uniform float uDebugTransparentFragmentMarker;
uniform float uDebugFluidFragmentMarker;

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

float materialNoise(vec2 p);

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

vec2 materialPlanePosition(int faceIndex, vec3 worldPos)
{
    if (faceIndex < 4) {
        return (faceIndex == 0 || faceIndex == 2)
            ? worldPos.xy
            : worldPos.zy;
    }
    return worldPos.xz;
}

uint hashMix32(uint value)
{
    value = value ^ (value >> 16u);
    value *= 0x7feb352du;
    value = value ^ (value >> 15u);
    value *= 0x846ca68bu;
    return value ^ (value >> 16u);
}

uint hashVoxelCell(ivec3 cell, uint salt)
{
    uint value = uint(cell.x) * 0x8da6b343u;
    value ^= uint(cell.y) * 0xd8163841u;
    value ^= uint(cell.z) * 0xcb1ab31fu;
    return hashMix32(value ^ salt);
}

float hashUnit(uint value)
{
    return float(value >> 8u) * (1.0 / 16777216.0);
}

float materialHash(vec2 p)
{
    ivec2 cell = ivec2(floor(p));
    return hashUnit(hashVoxelCell(ivec3(cell, 0), 0x68bc21ebu));
}

vec2 atlasUVFromWorldPos(int faceIndex, vec3 worldPos)
{
    const float kCubeShift = 1.0 / 6.0;
    float u0 = float(faceIndex) * kCubeShift;
    float u1 = float(faceIndex + 1) * kCubeShift;
    vec2 inset = atlasHalfTexelInset();
    vec2 tile;
    if (faceIndex == 0) {
        tile = vec2(blockTileCoord(worldPos.x), blockTileCoord(worldPos.y));
    } else if (faceIndex == 1) {
        tile = vec2(1.0 - blockTileCoord(worldPos.z), blockTileCoord(worldPos.y));
    } else if (faceIndex == 2) {
        tile = vec2(1.0 - blockTileCoord(worldPos.x), blockTileCoord(worldPos.y));
    } else if (faceIndex == 3) {
        tile = vec2(blockTileCoord(worldPos.z), blockTileCoord(worldPos.y));
    } else if (faceIndex == 4) {
        tile = vec2(blockTileCoord(worldPos.x), 1.0 - blockTileCoord(worldPos.z));
    } else {
        tile = vec2(blockTileCoord(worldPos.x), blockTileCoord(worldPos.z));
    }
    vec2 localTile = tile;
    // Water's small source tile repeats too clearly across broad surfaces.
    // Slowly warp its phase with a continuous world-space field so neighboring
    // blocks meet without seams while the texture pattern drifts naturally.
    if (uTextureVariation >= 0.34 && uTextureVariation <= 0.40 && faceIndex >= 4) {
        vec2 waterPlane = worldPos.xz;
        vec2 phaseWarp = vec2(
            materialNoise(waterPlane * 0.27 + vec2(17.8, 9.3)),
            materialNoise(waterPlane * 0.27 + vec2(3.2, 29.6))
        ) - 0.5;
        tile = fract(tile + phaseWarp * 1.08);
    }
    // Keep one continuous, unmodified tile as the seam-safe fallback for the
    // randomized interior region below.
    vec2 seamlessTile = tile;
    // Per-block discrete tile transforms break large repeated ground and rock
    // patterns. Blend them back to the continuous tile at face boundaries so
    // randomized rotations and phase shifts cannot create a block-sized seam.
    // Structured blocks may still use safe rotations on their isotropic top
    // and bottom tiles. On directional side faces, vary only the horizontal
    // phase so bark, grass layers, and masonry keep their intended orientation.
    // Water already receives continuous world-space UV warping and lighting
    // below. Independent per-voxel transforms leave square brightness seams
    // on lakes, so keep its atlas coordinates continuous across block edges.
    bool canTransformTile = uTextureVariation >= 0.75 ||
                            (uTextureVariation >= 0.45 && faceIndex >= 4);
    // Bark uses the 0.52-0.74 range: keep its vertical grain unrotated, while
    // allowing both-axis texel phase changes so stacked logs do not form the
    // same horizontal bands at every block boundary. Grass-side layers stay
    // below 0.52 and only shift horizontally, preserving their top edge.
    bool directionalPhaseOnly = uTextureVariation >= 0.45 &&
                                 uTextureVariation < 0.75 && faceIndex < 4;
    if (canTransformTile || directionalPhaseOnly) {
        ivec3 cell = ivec3(floor(worldPos + vec3(0.5)));
        uint faceSalt = uint(faceIndex + 1) * 0x9e3779b9u;
        uint seed = hashVoxelCell(cell, faceSalt);
        vec2 randomizedTile = tile;
        if (canTransformTile) {
            float transform = floor(hashUnit(hashMix32(seed ^ 0xa511e9b3u)) * 8.0);
            float rotation = mod(transform, 4.0);
            if (rotation < 0.5) {
                randomizedTile = vec2(randomizedTile.y, 1.0 - randomizedTile.x);
            } else if (rotation < 1.5) {
                randomizedTile = vec2(1.0 - randomizedTile.x, 1.0 - randomizedTile.y);
            } else if (rotation < 2.5) {
                randomizedTile = vec2(1.0 - randomizedTile.y, randomizedTile.x);
            }
            if (transform >= 4.0) {
                randomizedTile.x = 1.0 - randomizedTile.x;
            }
        }
        vec2 tileResolution = vec2(textureSize(texture0, 0));
        tileResolution.x /= 6.0;
        tileResolution.y /= max(1.0, float(uAnimFrameCount));
        vec2 offsetRandom = vec2(
            hashUnit(hashMix32(seed ^ 0x63d83595u)),
            hashUnit(hashMix32(seed ^ 0xb5297a4du))
        );
        if (directionalPhaseOnly && uTextureVariation < 0.52) {
            offsetRandom.y = 0.0;
        }
        // Keep randomized offsets on texel boundaries so nearest-filtered
        // 16px materials do not become subpixel-blurred or unevenly tiled.
        vec2 offset = floor(offsetRandom * tileResolution * clamp(uTextureVariation, 0.0, 1.0)) / tileResolution;
        randomizedTile = fract(randomizedTile + offset);
        vec2 edgeTexels = min(localTile, vec2(1.0) - localTile) * tileResolution;
        float edgeDistanceTexels = min(edgeTexels.x, edgeTexels.y);
        // Preserve crisp pixel art: reserve the outer texel for the shared
        // tile, then switch to the deterministic transform without blending.
        float transformWeight = step(1.0, edgeDistanceTexels);
        tile = mix(seamlessTile, randomizedTile, transformWeight);
    }
    float v0 = faceIndex < 4 ? 1.0 : 0.0;
    float v1 = faceIndex < 4 ? 0.0 : 1.0;
    return vec2(insetMix(u0, u1, tile.x, inset.x),
                insetMix(v0, v1, tile.y, inset.y));
}

vec2 crossAtlasUV(vec2 meshUV)
{
    const float kCubeShift = 1.0 / 6.0;
    float u0 = 0.0;
    float u1 = kCubeShift;
    vec2 inset = atlasHalfTexelInset();
    vec2 tile = meshUV;
    if (uTextureVariation >= 0.75) {
        ivec3 cell = ivec3(floor(vInstanceCell + vec3(0.5)));
        uint seed = hashVoxelCell(cell, 0x4f1bbcdcu);
        float transform = floor(hashUnit(hashMix32(seed ^ 0xa511e9b3u)) * 8.0);
        float rotation = mod(transform, 4.0);
        if (rotation < 0.5) {
            tile = vec2(tile.y, 1.0 - tile.x);
        } else if (rotation < 1.5) {
            tile = vec2(1.0 - tile.x, 1.0 - tile.y);
        } else if (rotation < 2.5) {
            tile = vec2(1.0 - tile.y, tile.x);
        }
        if (transform >= 4.0) {
            tile.x = 1.0 - tile.x;
        }
    }
    return vec2(insetMix(u0, u1, tile.x, inset.x), insetMix(1.0, 0.0, tile.y, inset.y));
}

float materialNoise(vec2 p)
{
    vec2 cell = floor(p);
    vec2 f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    float n00 = materialHash(cell);
    float n10 = materialHash(cell + vec2(1.0, 0.0));
    float n01 = materialHash(cell + vec2(0.0, 1.0));
    float n11 = materialHash(cell + vec2(1.0, 1.0));
    return mix(mix(n00, n10, f.x), mix(n01, n11, f.x), f.y);
}

vec4 materialVariation(vec2 planePosition)
{
    return vec4(
        materialNoise(planePosition * 0.16 + vec2(13.7, 3.1)),
        materialNoise(planePosition * 0.16 + vec2(27.2, 11.3)),
        materialNoise(planePosition * 0.43 + vec2(4.9, 23.1)),
        materialNoise(planePosition * 0.43 + vec2(18.4, 7.7))
    );
}

float worldGrainNoise(vec2 planePosition)
{
    vec2 texelResolution = vec2(textureSize(texture0, 0));
    texelResolution.x /= 6.0;
    texelResolution.y /= max(1.0, float(uAnimFrameCount));
    // A two-texel lattice makes small coherent grain patches instead of
    // independent salt-and-pepper pixels. The lattice is world-aligned and
    // matches across neighboring block faces.
    vec2 latticePosition = (planePosition + vec2(0.5)) * texelResolution * 0.5;
    vec2 cell = floor(latticePosition);
    vec2 f = fract(latticePosition);
    f = f * f * (3.0 - 2.0 * f);
    uint materialSalt = uint(uMaterialSeed) * 0x9e3779b9u;
    materialSalt ^= uint(vFaceIndex + 1) * 0x85ebca6bu;
    ivec2 cellCoord = ivec2(cell);
    float n00 = hashUnit(hashVoxelCell(
        ivec3(cellCoord, vFaceIndex), materialSalt ^ 0x68bc21ebu
    ));
    float n10 = hashUnit(hashVoxelCell(
        ivec3(cellCoord + ivec2(1, 0), vFaceIndex), materialSalt ^ 0x68bc21ebu
    ));
    float n01 = hashUnit(hashVoxelCell(
        ivec3(cellCoord + ivec2(0, 1), vFaceIndex), materialSalt ^ 0x68bc21ebu
    ));
    float n11 = hashUnit(hashVoxelCell(
        ivec3(cellCoord + ivec2(1, 1), vFaceIndex), materialSalt ^ 0x68bc21ebu
    ));
    return mix(mix(n00, n10, f.x), mix(n01, n11, f.x), f.y);
}

float waterSurfaceLight(vec2 planePosition)
{
    // Use a warped, multi-scale world-space field. Independent smooth fields
    // at rotated scales break the large patches into irregular ripples while
    // keeping the result continuous across neighboring fluid blocks.
    float phase = uMaterialTimePhase * 6.28318530718;
    vec2 slowFlow = vec2(cos(phase * 0.31), sin(phase * 0.23)) * 0.38;
    vec2 fastFlow = vec2(sin(phase * 0.73), cos(phase * 0.59)) * 0.19;
    vec2 coarseWarp = vec2(
        materialNoise(planePosition * 0.105 + vec2(11.6, 24.2) + slowFlow),
        materialNoise(planePosition * 0.105 + vec2(26.4, 6.8) - slowFlow)
    ) - 0.5;
    vec2 fineWarp = vec2(
        materialNoise(planePosition * 0.29 + vec2(16.2, 12.7) + fastFlow),
        materialNoise(planePosition * 0.29 + vec2(7.9, 22.5) - fastFlow)
    ) - 0.5;
    vec2 warped = planePosition + coarseWarp * 4.4 + fineWarp * 1.15;
    vec2 rotatedA = vec2(warped.x * 0.83 - warped.y * 0.56,
                         warped.x * 0.56 + warped.y * 0.83);
    vec2 rotatedB = vec2(warped.x * 0.39 + warped.y * 0.92,
                         -warped.x * 0.92 + warped.y * 0.39);
    float swell = materialNoise(rotatedA * 0.092 + vec2(8.7, 16.1) + slowFlow * 0.58);
    float current = materialNoise(rotatedB * 0.245 + vec2(21.4, 4.8) + slowFlow + fastFlow);
    float ripple = materialNoise(rotatedA * 0.61 + vec2(3.2, 29.6) + fastFlow * 2.1);
    float microRipple = materialNoise(rotatedB * 1.34 + vec2(19.3, 2.1) + fastFlow * 2.8);
    float grain = worldGrainNoise(planePosition * 0.45 + slowFlow * 0.22);
    // Thresholded highlights are broken up by both the fine animated field
    // and a small world-aligned grain layer; this avoids parallel wave bands
    // and a visible one-tile motif over broad water surfaces.
    float crest = smoothstep(0.74, 0.96, ripple)
                  * smoothstep(0.22, 0.82, current);
    float grainCrest = smoothstep(0.79, 0.98, mix(microRipple, grain, 0.34))
                       * smoothstep(0.18, 0.84, ripple);
    return clamp(0.96 + (swell - 0.5) * 0.20
                 + (current - 0.5) * 0.28 + (ripple - 0.5) * 0.20
                 + (microRipple - 0.5) * 0.12 + (grain - 0.5) * 0.12
                 + crest * 0.12 + grainCrest * 0.06, 0.68, 1.32);
}

void main()
{
    vec2 uv;
    vec2 materialPlane = vec2(0.0);
    vec4 materialFields = vec4(0.5);
    if (vFaceIndex != kCrossFaceIndex && uTextureVariation > 0.001) {
        materialPlane = materialPlanePosition(vFaceIndex, vWorldPos);
        materialFields = materialVariation(materialPlane);
    }
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
    if (vFaceIndex != kCrossFaceIndex && uTextureVariation > 0.001) {
        float variation = clamp(uTextureVariation, 0.0, 1.0);
        bool waterSurface = variation >= 0.34 && variation <= 0.40;
        if (waterSurface) {
            float waterLight = waterSurfaceLight(materialPlane);
            FragColor.rgb *= waterLight;
            vec2 glintPlane = vec2(
                materialPlane.x * 0.86 - materialPlane.y * 0.51,
                materialPlane.x * 0.51 + materialPlane.y * 0.86
            );
            float glintField = materialNoise(
                glintPlane * 2.65 + vec2(17.8, 9.3)
                + (materialFields.zw - 0.5) * 0.44
                + vec2(sin(uMaterialTimePhase * 18.8495559),
                       cos(uMaterialTimePhase * 12.5663706)) * 0.8
            );
            float glint = smoothstep(0.80, 0.97, glintField);
            FragColor.rgb = mix(FragColor.rgb, vec3(0.30, 0.66, 0.91), glint * 0.26);
        } else {
            float spatialTone = mix(materialFields.x, materialFields.w, 0.26);
            float fineTone = materialNoise(
                materialPlane * 7.5 + vec2(13.1, 5.7)
                + (materialFields.zw - 0.5) * 0.68
            );
            bool naturalSurface = variation >= 0.75;
            spatialTone = mix(spatialTone, fineTone, naturalSurface ? 0.36 : 0.72);
            float fineGrain = naturalSurface
                ? worldGrainNoise(materialPlane)
                : materialNoise(materialPlane * 15.0 + vec2(7.3, 29.1));
            float broadAmplitude = (naturalSurface ? 0.20 : 0.30) * variation;
            float grainAmplitude = (naturalSurface ? 0.22 : 0.18) * variation;
            FragColor.rgb *= 1.0 + (spatialTone - 0.5) * broadAmplitude
                             + (fineGrain - 0.5) * grainAmplitude;
        }
    }
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
    float wet = clamp(vWetness * uEnvWetness, 0.0, 1.0);
    if (wet > 0.001) {
        vec3 cooler = FragColor.rgb * vec3(0.78, 0.82, 0.9);
        FragColor.rgb = mix(FragColor.rgb, cooler, wet * 0.08);
    }
    // Keep the opt-in desktop/GLES fluid discard probe behavior aligned.
    if (uDebugFluidFragmentMarker > 0.5 &&
        uGreedyShaderMode == kGreedyModeColor) {
        bool alpha_cutout_candidate =
            uAlphaCutout != 0 && FragColor.a < 0.1;
        bool depth_guard_candidate = false;
        if (uOpaqueDepthGuard > 0.5 &&
            uOpaqueDepthScreenSize.x > 0.0) {
            vec2 depthUv = gl_FragCoord.xy / uOpaqueDepthScreenSize;
            float opaqueDepth = texture(uOpaqueDepthMap, depthUv).r;
            depth_guard_candidate =
                gl_FragCoord.z > opaqueDepth + uOpaqueDepthBias;
        }
        if (alpha_cutout_candidate && depth_guard_candidate) {
            FragColor = vec4(1.0, 1.0, 0.0, 1.0);
        } else if (alpha_cutout_candidate) {
            FragColor = vec4(1.0, 0.0, 0.0, 1.0);
        } else if (depth_guard_candidate) {
            FragColor = vec4(0.0, 0.0, 1.0, 1.0);
        } else {
            FragColor = vec4(1.0, 0.0, 1.0, 1.0);
        }
        return;
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
    if (uDebugTransparentFragmentMarker > 0.5) {
        FragColor = vec4(1.0, 0.0, 1.0, 1.0);
        return;
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
            float min_blend = uUnderwaterFogMinBlend;
            if (uUnderwaterFogSubmerged < 0.5) {
                // Camera above the surface: the tint comes from the water
                // thickness along the view ray, not from camera distance.
                vec3 view_dir = (vWorldPos - uCameraPos) / max(dist, 0.001);
                dist = (sy - vWorldPos.y) / max(abs(view_dir.y), 0.25);
                min_blend = 0.0;
            }
            float fog_factor = computeDistanceFogFactor(
                dist, uUnderwaterFogStart, uUnderwaterFogEnd,
                min_blend, 1.0);
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

