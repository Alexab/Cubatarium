#ifndef CREATUREDRAWSINK_H
#define CREATUREDRAWSINK_H

#include "Creatures/Visual/CreatureDrawRequest.h"
#include <glm/glm.hpp>
#include <memory>
#include <vector>

typedef unsigned int GLuint;

namespace cutum
{

/// Draw target for the creature queue. The main render pass and the GUI
/// preview both implement it, so backends submit identically in-game and in
/// preview (audit 2.5 — the preview used to re-implement every backend).
class ICreatureDrawSink
{
public:
  virtual ~ICreatureDrawSink() = default;
  virtual void DrawTexturedPart(const glm::mat4 &mvp, GLuint texture,
                                CreaturePartMesh mesh) = 0;
  virtual void DrawBoneSkeletonMesh(const glm::mat4 &mvp, GLuint texture,
                                    const BoneSkeletonCubeMeshCpu &mesh) = 0;
  virtual void DrawSkinnedMesh(
      const glm::mat4 &mvp, GLuint texture, const GltfPrimitiveCpu &mesh,
      const std::shared_ptr<const std::vector<glm::mat4>> &boneMatrices) = 0;
  virtual void DrawBoxWireframe(const glm::mat4 &mvp,
                                const glm::vec4 &color) = 0;
  virtual void DrawBillboard(const glm::mat4 &mvp, GLuint texture,
                             const glm::vec4 &tint) = 0;
};

} // namespace cutum

#endif
