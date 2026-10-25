#ifndef CREATUREDRAWREQUEST_H
#define CREATUREDRAWREQUEST_H

#include "Creatures/Visual/CreaturePartMeshData.h"
#include "Creatures/Visual/Gltf/CreatureGltfTypes.h"
#include <glm/glm.hpp>
#include <memory>
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
  Billboard,
};

struct CreatureDrawRequest
{
  CreatureDrawKind Kind{CreatureDrawKind::TexturedPart};
  glm::mat4 Mvp{1.f};
  GLuint Texture{0};
  CreaturePartMesh PartMesh{CreaturePartMesh::Box};
  const BoneSkeletonCubeMeshCpu *SkeletalMesh{nullptr};
  const GltfPrimitiveCpu *SkinnedPrimitive{nullptr};
  /// Shared per-creature palette: primitives of one creature reference the
  /// same immutable vector instead of copying it per primitive (audit V7).
  std::shared_ptr<const std::vector<glm::mat4>> BoneMatrices;
  glm::vec4 WireColor{1.f};
  glm::vec4 Tint{1.f};
};

} // namespace cutum

#endif
