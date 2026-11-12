#pragma once
#include "Creatures/Visual/CreatureDrawSink.h"
#include <limits>

namespace cutum
{
// Feed the real visual's requests with identity view-projection. Matrices then
// transform the same submitted geometry into entity/world space, without a
// second procedural-animation implementation or dependence on collider size.
class CreaturePreviewBoundsSink final : public ICreatureDrawSink
{
public:
  glm::vec3 low{std::numeric_limits<float>::max()}, high{-std::numeric_limits<float>::max()};
  bool any{false};
  void Point(const glm::mat4 &model, const glm::vec4 &point)
  {
    const glm::vec3 p(model * point);
    low = glm::min(low, p); high = glm::max(high, p); any = true;
  }
  void DrawTexturedPart(const glm::mat4 &model, GLuint, CreaturePartMesh) override
  {
    for (int x : {-1, 1}) for (int y : {-1, 1}) for (int z : {-1, 1})
      Point(model, glm::vec4(x * .5f, y * .5f, z * .5f, 1.f));
  }
  void DrawBoneSkeletonMesh(const glm::mat4 &model, GLuint, const BoneSkeletonCubeMeshCpu &mesh) override
  {
    const auto &v = mesh.interleavedPosUv;
    for (size_t i = 0; i + 4 < v.size(); i += 5) Point(model, glm::vec4(v[i], v[i+1], v[i+2], 1.f));
  }
  void DrawSkinnedMesh(const glm::mat4 &model, GLuint, const GltfPrimitiveCpu &mesh,
      const std::shared_ptr<const std::vector<glm::mat4>> &palette) override
  {
    const auto &v = mesh.mesh.interleavedPosUv;
    for (size_t i = 0; i + 4 < v.size(); i += 5)
    {
      const glm::vec4 p(v[i], v[i+1], v[i+2], 1.f);
      glm::vec4 posed = p;
      const size_t index = i / 5 * 4;
      if (mesh.skinned && palette && index + 3 < mesh.jointIndices.size() && index + 3 < mesh.jointWeights.size())
      {
        posed = glm::vec4(0.f);
        for (size_t j = 0; j < 4; ++j)
          if (mesh.jointWeights[index+j] > 0 && mesh.jointIndices[index+j] < palette->size())
            posed += (*palette)[mesh.jointIndices[index+j]] * p * mesh.jointWeights[index+j];
      }
      Point(model, posed);
    }
  }
  void DrawBoxWireframe(const glm::mat4 &, const glm::vec4 &) override {}
  void DrawBillboard(const glm::mat4 &model, GLuint, const glm::vec4 &) override
  {
    for (int x : {-1, 1}) for (int y : {-1, 1}) Point(model, glm::vec4(x*.5f, y*.5f, 0.f, 1.f));
  }
};
} // namespace cutum
