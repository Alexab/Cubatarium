#ifndef CREATUREENGINEDRAWSINK_H
#define CREATUREENGINEDRAWSINK_H

#include "Creatures/Visual/CreatureDrawSink.h"
#include "Render/Engine/GeometryEngine.h"

namespace cutum
{

/// Queue sink that dispatches into the main geometry engine.
class UEngineCreatureDrawSink final : public ICreatureDrawSink
{
public:
  explicit UEngineCreatureDrawSink(UGeometryEngine &engine) : Engine(engine) {}

  void DrawTexturedPart(const glm::mat4 &mvp, GLuint texture,
                        CreaturePartMesh mesh) override
  {
    Engine.DrawCreatureTexturedPart(mvp, texture, mesh);
  }
  void DrawBoneSkeletonMesh(const glm::mat4 &mvp, GLuint texture,
                            const BoneSkeletonCubeMeshCpu &mesh) override
  {
    Engine.DrawCreatureBoneSkeletonMesh(mvp, texture, mesh);
  }
  void DrawSkinnedMesh(
      const glm::mat4 &mvp, GLuint texture, const GltfPrimitiveCpu &mesh,
      const std::shared_ptr<const std::vector<glm::mat4>> &boneMatrices) override
  {
    Engine.DrawCreatureSkinnedMesh(mvp, texture, mesh, boneMatrices);
  }
  void DrawBoxWireframe(const glm::mat4 &mvp, const glm::vec4 &color) override
  {
    Engine.DrawBoxWireframe(mvp, color);
  }
  void DrawBillboard(const glm::mat4 &mvp, GLuint texture,
                     const glm::vec4 &tint) override
  {
    Engine.DrawCreatureBillboard(mvp, texture, tint);
  }

private:
  UGeometryEngine &Engine;
};

} // namespace cutum

#endif
