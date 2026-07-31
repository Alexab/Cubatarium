#version 430 core

layout(std430, binding = 0) readonly buffer QuadBuf { uvec2 quads[]; };

uniform mat4 mvp_matrix;
uniform ivec3 chunkOrigin;

out vec3 vWorldPos;
flat out int vFaceIndex;
out float vSkyLight;
out float vBlockLight;
out float vWetness;

void main()
{
    int quadIdx = gl_VertexID / 6;
    int corner  = gl_VertexID % 6;

    uvec2 q = quads[quadIdx];
    uint w0 = q.x, w1 = q.y;

    float x = float(w0 & 0x1Fu);
    float y = float((w0 >> 5u) & 0x1Fu);
    float z = float((w0 >> 10u) & 0x1Fu);
    float qw = float((w0 >> 15u) & 0x1Fu);
    float qh = float((w0 >> 20u) & 0x1Fu);
    int face = int((w0 >> 25u) & 0x7u);

    float sky = float((w1 >> 10u) & 0xFu) / 15.0;
    float blk = float((w1 >> 14u) & 0xFu) / 15.0;

    vec3 pos = vec3(x, y, z);
    vec3 du, dv;

    if (face == 0)      { pos.z += 1.0; du = vec3(1,0,0); dv = vec3(0,1,0); }
    else if (face == 1) { pos.x += 1.0; du = vec3(0,0,1); dv = vec3(0,1,0); }
    else if (face == 2) { du = vec3(-1,0,0); dv = vec3(0,1,0); pos.x += qw; }
    else if (face == 3) { du = vec3(0,0,-1); dv = vec3(0,1,0); pos.z += qw; }
    else if (face == 4) { pos.y += 1.0; du = vec3(1,0,0); dv = vec3(0,0,1); }
    else                { du = vec3(1,0,0); dv = vec3(0,0,-1); pos.z += qh; }

    vec3 offset;
    if      (corner == 0) offset = vec3(0);
    else if (corner == 1 || corner == 3) offset = du * qw;
    else if (corner == 2 || corner == 4) offset = du * qw + dv * qh;
    else                  offset = dv * qh;

    vec3 worldPos = vec3(chunkOrigin) + pos + offset;
    gl_Position = mvp_matrix * vec4(worldPos, 1.0);
    vWorldPos = worldPos;
    vFaceIndex = face;
    vSkyLight = sky;
    vBlockLight = blk;
    vWetness = 0.0;
}
