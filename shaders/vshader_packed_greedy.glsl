#version 430 core

layout(std430, binding = 0) readonly buffer QuadBuf { uvec2 quads[]; };

uniform mat4 mvp_matrix;
uniform ivec3 chunkOrigin;

out vec3 vWorldPos;
flat out vec3 vInstanceCell;
flat out int vFaceIndex;
out float vSkyLight;
out float vBlockLight;
out float vWetness;
flat out float vLightPreview;

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

    // Keep the packed vertex-pulling path on the same half-centered voxel
    // grid as GreedyMeshEmitter: its local face origin is u/v/slice - 0.5.
    vec3 pos = vec3(x, y, z) - vec3(0.5);
    vec3 du, dv;

    // Packed emit preserves the GPU greedy grid's axes:
    // u=(axis+1)%3 and v=(axis+2)%3. Keep du/dv and the negative-face
    // origin aligned with that same ordering so width/height cover the mesh.
    if (face == 0)      { pos.z += 1.0; du = vec3(1,0,0);  dv = vec3(0,1,0); }
    else if (face == 1) { pos.x += 1.0; du = vec3(0,1,0);  dv = vec3(0,0,1); }
    else if (face == 2) { du = vec3(-1,0,0); dv = vec3(0,1,0); pos.x += qw; }
    else if (face == 3) { du = vec3(0,-1,0); dv = vec3(0,0,1); pos.y += qw; }
    else if (face == 4) { pos.y += 1.0; du = vec3(0,0,1);  dv = vec3(1,0,0); }
    else                { du = vec3(0,0,-1); dv = vec3(1,0,0); pos.z += qw; }

    // Quad corners: 0=origin, 1=+u, 2=+u+v, 3=+v. Triangles must be
    // (0,1,2),(0,2,3) exactly like GreedyMeshEmitter::AppendGreedyQuad;
    // packed_quad_parity_test parses this table.
    const int kCornerIdx[6] = int[6](0, 1, 2, 0, 2, 3);
    const int ci = kCornerIdx[corner];
    vec3 offset;
    if      (ci == 0) offset = vec3(0);
    else if (ci == 1) offset = du * qw;
    else if (ci == 2) offset = du * qw + dv * qh;
    else              offset = dv * qh;

    vec3 worldPos = vec3(chunkOrigin) + pos + offset;
    gl_Position = mvp_matrix * vec4(worldPos, 1.0);
    vWorldPos = worldPos;
    vInstanceCell = floor(worldPos + vec3(0.5));
    vFaceIndex = face;
    vSkyLight = sky;
    vBlockLight = blk;
    vWetness = 0.0;
    vLightPreview = float((w1 >> 24u) & 0x1u);
}
