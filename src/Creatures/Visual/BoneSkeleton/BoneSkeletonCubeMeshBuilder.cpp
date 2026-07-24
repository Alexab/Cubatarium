#include "Creatures/Visual/BoneSkeleton/BoneSkeletonCubeMeshBuilder.h"

#include "Creatures/Visual/BoneSkeleton/BoneSkeletonModelSpace.h"
#include "Creatures/Visual/CreaturePartMeshData.h"
#include <glm/gtc/matrix_transform.hpp>

namespace cutum
{

namespace
{

struct FaceUvPixels
{
  int u0, v0, u1, v1;
};

enum class UvCorner : uint8_t
{
  Bl,
  Br,
  Tl,
  Tr,
};

// Face order in kCreaturePartPositions / GeometryEngine cube buffers:
//   0 = +Z south (back), 1 = +X east (mob right), 2 = -Z north (front),
//   3 = -X west (mob left), 4 = +Y up, 5 = -Y down.
constexpr UvCorner kFaceUvCorners[6][4] = {
    {UvCorner::Bl, UvCorner::Br, UvCorner::Tl, UvCorner::Tr}, // +Z south
    {UvCorner::Bl, UvCorner::Br, UvCorner::Tl, UvCorner::Tr}, // +X east
    {UvCorner::Br, UvCorner::Bl, UvCorner::Tr, UvCorner::Tl}, // -Z north
    {UvCorner::Tr, UvCorner::Br, UvCorner::Tl, UvCorner::Bl}, // -X west
    {UvCorner::Bl, UvCorner::Br, UvCorner::Tl, UvCorner::Tr}, // +Y up
    {UvCorner::Bl, UvCorner::Br, UvCorner::Tr, UvCorner::Tl}, // -Y down
};

int PixelDim(float blocks)
{
  return std::max(1, static_cast<int>(std::lround(blocks * kBoneSkeletonUnitsPerBlock)));
}

FaceUvPixels FacePixels(int faceIndex, int u, int v, int w, int h, int d)
{
  // Box UV unfold: W=w, H=h, D=d at atlas (u,v). See wiki.vg / Mojang samples.
  switch (faceIndex)
  {
  case 0: // +Z south (back)
    return {u + d + w + d, v + d, u + d + w + d + w, v + d + h};
  case 1: // +X east (mob right)
    return {u + d + w, v + d, u + d + w + d, v + d + h};
  case 2: // -Z north (front)
    return {u + d, v + d, u + d + w, v + d + h};
  case 3: // -X west (mob left)
    return {u, v + d, u + d, v + d + h};
  case 4: // +Y up
    return {u + d, v, u + d + w, v + d};
  case 5: // -Y down
    return {u + d + w, v, u + d + w + w, v + d};
  default:
    return {u, v, u + 1, v + 1};
  }
}

void GlUvForCorner(UvCorner corner, float u0, float v0, float u1, float v1,
                   float &outU, float &outV)
{
  switch (corner)
  {
  case UvCorner::Bl:
    outU = u0;
    outV = v1;
    break;
  case UvCorner::Br:
    outU = u1;
    outV = v1;
    break;
  case UvCorner::Tl:
    outU = u0;
    outV = v0;
    break;
  case UvCorner::Tr:
    outU = u1;
    outV = v0;
    break;
  }
}

void AppendFace(float *outPosUv, int &idx, int faceIndex, float u0, float v0,
                float u1, float v1, bool mirrorEastWest)
{
  if (mirrorEastWest && (faceIndex == 1 || faceIndex == 3))
  {
    std::swap(u0, u1);
  }
  const int base = faceIndex * 4;
  for (int i = 0; i < 4; ++i)
  {
    outPosUv[idx++] = kCreaturePartPositions[(base + i) * 3 + 0];
    outPosUv[idx++] = kCreaturePartPositions[(base + i) * 3 + 1];
    outPosUv[idx++] = kCreaturePartPositions[(base + i) * 3 + 2];
    float tu = 0.f;
    float tv = 0.f;
    GlUvForCorner(kFaceUvCorners[faceIndex][i], u0, v0, u1, v1, tu, tv);
    outPosUv[idx++] = tu;
    outPosUv[idx++] = tv;
  }
}

void PushFaceIndices(std::vector<unsigned int> &indices, unsigned int vertBase)
{
  indices.push_back(vertBase + 0);
  indices.push_back(vertBase + 1);
  indices.push_back(vertBase + 2);
  indices.push_back(vertBase + 2);
  indices.push_back(vertBase + 1);
  indices.push_back(vertBase + 3);
}

glm::vec3 RenderSizeBlocks(const glm::vec3 &logicalSize)
{
  glm::vec3 render = logicalSize;
  const float thin = BoneSkeletonThinAxisBlocks();
  if (render.x <= 0.f)
  {
    render.x = thin;
  }
  if (render.y <= 0.f)
  {
    render.y = thin;
  }
  if (render.z <= 0.f)
  {
    render.z = thin;
  }
  return render;
}

glm::mat4 BuildCubeRestMatrix(const BoneSkeletonCubeDef &cube,
                              const glm::vec3 &bonePivotBlocks,
                              const glm::vec3 &logicalSize,
                              const glm::vec3 &renderSize)
{
  glm::vec3 origin = cube.originBlocks;
  if (cube.inflateBlocks > 0.f)
  {
    origin -= glm::vec3(cube.inflateBlocks);
  }
  if (logicalSize.x <= 0.f)
  {
    origin.x -= (renderSize.x - logicalSize.x) * 0.5f;
  }
  if (logicalSize.y <= 0.f)
  {
    origin.y -= (renderSize.y - logicalSize.y) * 0.5f;
  }
  if (logicalSize.z <= 0.f)
  {
    origin.z -= (renderSize.z - logicalSize.z) * 0.5f;
  }

  const glm::vec3 meshCenter = origin + renderSize * 0.5f;
  const glm::vec3 rotPivotBlocks =
      cube.hasPivot ? cube.pivotBlocks : bonePivotBlocks;
  const glm::vec3 boneLocalCenter = meshCenter - bonePivotBlocks;
  const glm::vec3 boneLocalRotPivot = rotPivotBlocks - bonePivotBlocks;

  glm::mat4 local = glm::translate(glm::mat4(1.f), boneLocalCenter);
  if (glm::length(cube.rotationDeg) > 0.001f)
  {
    local = glm::translate(local, boneLocalRotPivot);
    local = local * BoneSkeletonEulerDegToMat(cube.rotationDeg);
    local = glm::translate(local, -boneLocalRotPivot);
  }
  return glm::scale(local, renderSize);
}

} // namespace

BoneSkeletonCubeMeshCpu
BoneSkeletonCubeMeshBuilder::BuildCubeMesh(const BoneSkeletonCubeDef &cube,
                                      const glm::vec3 &bonePivotBlocks,
                                      const glm::ivec2 &textureSize,
                                      bool mirrorUv)
{
  BoneSkeletonCubeMeshCpu mesh;
  glm::vec3 logicalSize = cube.sizeBlocks;
  if (cube.inflateBlocks > 0.f)
  {
    logicalSize += glm::vec3(cube.inflateBlocks * 2.f);
  }
  if (logicalSize.x < 0.f || logicalSize.y < 0.f || logicalSize.z < 0.f)
  {
    return mesh;
  }
  if (logicalSize.x == 0.f && logicalSize.y == 0.f && logicalSize.z == 0.f)
  {
    return mesh;
  }

  const glm::vec3 renderSize = RenderSizeBlocks(logicalSize);
  const int w = PixelDim(cube.sizeBlocks.x);
  const int h = PixelDim(cube.sizeBlocks.y);
  const int d = PixelDim(cube.sizeBlocks.z);
  const int u = cube.uvPixels.x;
  const int v = cube.uvPixels.y;
  const float tw = static_cast<float>(textureSize.x);
  const float th = static_cast<float>(textureSize.y);

  float posUv[24 * 5];
  int idx = 0;
  unsigned int vertBase = 0;
  for (int face = 0; face < 6; ++face)
  {
    if (!BoneSkeletonCubeFaceVisible(face, logicalSize))
    {
      continue;
    }
    FaceUvPixels px = FacePixels(face, u, v, w, h, d);
    // Half-texel inset avoids sampling transparent atlas padding at face edges.
    constexpr float kUvInsetPx = 0.5f;
    const float nu0 = (static_cast<float>(px.u0) + kUvInsetPx) / tw;
    const float nv0 = (static_cast<float>(px.v0) + kUvInsetPx) / th;
    const float nu1 = (static_cast<float>(px.u1) - kUvInsetPx) / tw;
    const float nv1 = (static_cast<float>(px.v1) - kUvInsetPx) / th;
    AppendFace(posUv, idx, face, nu0, nv0, nu1, nv1, mirrorUv);
    PushFaceIndices(mesh.indices, vertBase);
    vertBase += 4;
  }
  mesh.interleavedPosUv.assign(posUv, posUv + idx);

  BoneSkeletonCubeDef cubeForMatrix = cube;
  if (cube.inflateBlocks > 0.f)
  {
    cubeForMatrix.originBlocks -= glm::vec3(cube.inflateBlocks);
  }
  mesh.restLocalMatrix =
      BuildCubeRestMatrix(cubeForMatrix, bonePivotBlocks, logicalSize, renderSize);

  return mesh;
}

CreatureBoneSkeletonMeshAsset
BoneSkeletonCubeMeshBuilder::BuildMeshAsset(const CreatureBoneSkeletonGeometry &geometry)
{
  CreatureBoneSkeletonMeshAsset asset;
  asset.geometry = geometry;
  asset.boneMeshes.reserve(geometry.bones.size());
  for (const BoneSkeletonBoneDef &bone : geometry.bones)
  {
    BoneSkeletonBoneMeshCpu boneMesh;
    boneMesh.boneName = bone.name;
    for (const BoneSkeletonCubeDef &cube : bone.cubes)
    {
      const bool mirrorUv = bone.mirror || cube.mirror;
      boneMesh.cubes.push_back(
          BuildCubeMesh(cube, bone.pivotBlocks, geometry.textureSize, mirrorUv));
    }
    asset.boneMeshes.push_back(std::move(boneMesh));
  }
  return asset;
}

} // namespace cutum
