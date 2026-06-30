#ifndef CREATUREDRAWREQUEST_H
#define CREATUREDRAWREQUEST_H

#include "Creatures/Visual/CreaturePartMeshData.h"
#include "Creatures/Visual/Gltf/CreatureGltfTypes.h"
#include <glm/glm.hpp>
#include <vector>

typedef unsigned int GLuint;

namespace cutum
{

enum class CreatureDrawKind
{
  TexturedPart,
  SkeletalMesh,
  SkinnedMesh,
  WireframeBox,
};

struct CreatureDrawRequest
{
  CreatureDrawKind Kind{CreatureDrawKind::TexturedPart};
  glm::mat4 Mvp{1.f};
  GLuint Texture{0};
  CreaturePartMesh PartMesh{CreaturePartMesh::Box};
  const SkeletalCubeMeshCpu *SkeletalMesh{nullptr};
  const GltfPrimitiveCpu *SkinnedPrimitive{nullptr};
  std::vector<glm::mat4> BoneMatrices;
  glm::vec4 WireColor{1.f};
};

} // namespace cutum

#endif
