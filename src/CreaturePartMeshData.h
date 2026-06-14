#ifndef CREATURE_PART_MESH_DATA_H
#define CREATURE_PART_MESH_DATA_H

#include <cmath>

namespace cutum {

// Unit cube positions (24 verts × xyz); face order matches GeometryEngine::InitCubeBuffers.
// Rig forward: local +Z (front atlas on +Z face). +Z face index 0, +X index 1.
inline constexpr float kCreaturePartPositions[] = {
    -0.5f, -0.5f, 0.5f, 0.5f, -0.5f, 0.5f, -0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f,
    0.5f, -0.5f, 0.5f, 0.5f, -0.5f, -0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, -0.5f,
    0.5f, -0.5f, -0.5f, -0.5f, -0.5f, -0.5f, 0.5f, 0.5f, -0.5f, -0.5f, 0.5f, -0.5f,
    -0.5f, -0.5f, -0.5f, -0.5f, -0.5f, 0.5f, -0.5f, 0.5f, -0.5f, -0.5f, 0.5f, 0.5f,
    -0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, -0.5f, 0.5f, -0.5f, 0.5f, 0.5f, -0.5f,
    -0.5f, -0.5f, -0.5f, 0.5f, -0.5f, -0.5f, -0.5f, -0.5f, 0.5f, 0.5f, -0.5f, 0.5f,
};

inline constexpr unsigned int kCreaturePartIndices[] = {
    0, 1, 2, 2, 1, 3, 4, 5, 6, 6, 5, 7, 8, 9, 10, 10, 9, 11,
    12, 13, 14, 14, 13, 15, 16, 17, 18, 18, 17, 19, 20, 21, 22, 22, 21, 23,
};

struct CreatureFaceUv {
    float u0, v0, u1, v1;
};

inline constexpr CreatureFaceUv kUvFull = {0.0f, 0.0f, 1.0f, 1.0f};
inline constexpr CreatureFaceUv kUvPlain = {0.02f, 0.02f, 0.18f, 0.18f};
inline constexpr CreatureFaceUv kUvFaceFront = {0.28f, 0.22f, 0.72f, 0.78f};
inline constexpr CreatureFaceUv kUvBodyFront = {0.28f, 0.22f, 0.72f, 0.78f};
inline constexpr CreatureFaceUv kUvBodyBack = {0.02f, 0.55f, 0.18f, 0.82f};
inline constexpr CreatureFaceUv kUvHair = {0.55f, 0.02f, 0.78f, 0.14f};

// World XZ direction (dx, dz) -> model yaw so local +Z aligns with movement.
inline float ModelYawFromDirection(float dirX, float dirZ)
{
    if (dirX * dirX + dirZ * dirZ < 1e-8f) {
        return 0.0f;
    }
    float yaw = static_cast<float>(std::atan2(static_cast<double>(dirX),
                                              static_cast<double>(dirZ)) *
                               57.2957795);
    if (yaw > 180.0f) {
        yaw -= 360.0f;
    } else if (yaw <= -180.0f) {
        yaw += 360.0f;
    }
    return yaw;
}

// UCamera look yaw (degrees) -> model yaw (local +Z forward).
inline float ModelYawFromCameraYaw(float cameraYawDeg)
{
    const float r = cameraYawDeg * 0.0174532925f;
    return ModelYawFromDirection(std::cos(r), std::sin(r));
}

inline float CameraYawFromModelYaw(float modelYawDeg)
{
    const float r = modelYawDeg * 0.0174532925f;
    return ModelYawFromDirection(std::cos(r), std::sin(r));
}

inline void AppendFaceUv(float* out, int& idx, CreatureFaceUv uv)
{
    out[idx++] = uv.u0;
    out[idx++] = uv.v1;
    out[idx++] = uv.u1;
    out[idx++] = uv.v1;
    out[idx++] = uv.u0;
    out[idx++] = uv.v0;
    out[idx++] = uv.u1;
    out[idx++] = uv.v0;
}

inline void BuildCreatureBoxTexCoords(float* out)
{
    int i = 0;
    for (int f = 0; f < 6; ++f) {
        AppendFaceUv(out, i, kUvFull);
    }
}

inline void BuildCreatureHeadTexCoords(float* out)
{
    int i = 0;
    AppendFaceUv(out, i, kUvFaceFront); // +Z forward
    AppendFaceUv(out, i, kUvPlain);     // +X arm side
    AppendFaceUv(out, i, kUvPlain);     // -Z
    AppendFaceUv(out, i, kUvPlain);     // -X arm side
    AppendFaceUv(out, i, kUvHair);      // +Y
    AppendFaceUv(out, i, kUvPlain);     // -Y
}

inline void BuildCreatureBodyTexCoords(float* out)
{
    int i = 0;
    AppendFaceUv(out, i, kUvBodyFront); // +Z front (buttons)
    AppendFaceUv(out, i, kUvPlain);     // +X
    AppendFaceUv(out, i, kUvBodyBack);  // -Z back
    AppendFaceUv(out, i, kUvPlain);     // -X
    AppendFaceUv(out, i, kUvPlain);     // +Y
    AppendFaceUv(out, i, kUvPlain);     // -Y
}

enum class CreaturePartMesh {
    Box,
    Head,
    Body,
};

} // namespace cutum

#endif
