#version 300 es
precision mediump float;

in vec3 vWorldPos;
flat in int vFaceIndex;
out vec4 FragColor;

uniform sampler2D texture0;
uniform mediump int uAnimFrame;
uniform mediump int uAnimFrameCount;

float voxelTile(float w)
{
    return fract(w + 0.5) - 0.5;
}

vec2 atlasHalfTexelInset()
{
    ivec2 atlasSize = textureSize(texture0, 0);
    vec2 safeSize = vec2(max(float(atlasSize.x), 1.0), max(float(atlasSize.y), 1.0));
    return 0.5 / safeSize;
}

float insetMix(float a, float b, float t, float inset)
{
    float span = b - a;
    float dir = sign(span);
    float safeInset = min(inset, abs(span) * 0.25);
    return mix(a + dir * safeInset, b - dir * safeInset, t);
}

vec2 atlasUVFromWorldPos(int face, vec3 wp)
{
    float cubeShift = 1.0 / 6.0;
    float u0 = float(face) * cubeShift;
    float u1 = float(face + 1) * cubeShift;
    vec2 inset = atlasHalfTexelInset();

    float tx = voxelTile(wp.x);
    float ty = voxelTile(wp.y);
    float tz = voxelTile(wp.z);

    if (face == 0) {
        return vec2(insetMix(u0, u1, tx, inset.x), insetMix(1.0, 0.0, ty, inset.y));
    }
    if (face == 1) {
        return vec2(insetMix(u0, u1, 1.0 - tz, inset.x), insetMix(1.0, 0.0, ty, inset.y));
    }
    if (face == 2) {
        return vec2(insetMix(u0, u1, 1.0 - tx, inset.x), insetMix(1.0, 0.0, ty, inset.y));
    }
    if (face == 3) {
        return vec2(insetMix(u0, u1, tz, inset.x), insetMix(1.0, 0.0, ty, inset.y));
    }
    if (face == 4) {
        return vec2(insetMix(u0, u1, tx, inset.x), insetMix(0.0, 1.0, 1.0 - tz, inset.y));
    }
    return vec2(insetMix(u0, u1, tx, inset.x), insetMix(0.0, 1.0, tz, inset.y));
}

void main()
{
    vec2 uv = atlasUVFromWorldPos(vFaceIndex, vWorldPos);
    if (uAnimFrameCount > 1) {
        float frameH = 1.0 / float(uAnimFrameCount);
        uv.y = uv.y * frameH + float(uAnimFrame) * frameH;
    }
    FragColor = texture(texture0, uv);
}
