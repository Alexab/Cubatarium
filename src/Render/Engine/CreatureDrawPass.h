#ifndef CREATUREDRAWPASS_H
#define CREATUREDRAWPASS_H

#include "App/Settings/RenderSettings.h"
#include "Creatures/Visual/CreatureDrawContext.h"
#include "Creatures/Visual/CreatureDrawQueue.h"
#include "Creatures/Visual/CreaturePartMeshData.h"
#include "Creatures/Visual/CreatureRenderStats.h"
#include "Creatures/Visual/Gltf/CreatureGltfTypes.h"
#include "Render/Engine/ShaderManager.h"
#include <glm/glm.hpp>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

typedef unsigned int GLuint;

namespace cutum
{

class UCreature;
class UCreatureTextureStorage;
struct CreatureDefinition;
class UGeometryEngine;
class UShaderProgram;
class UWorld;

class UCreatureDrawPass
{
public:
  bool InitBuffers(UShaderManager &shader_manager);
  void DestroyBuffers();

  void Render(UWorld &world, UGeometryEngine &engine,
              const RenderSettings &render);

  void DrawTexturedPart(const glm::mat4 &mvp, GLuint texture,
                        CreaturePartMesh mesh = CreaturePartMesh::Box);
  void DrawBoneSkeletonMesh(const glm::mat4 &mvp, GLuint texture,
                            const BoneSkeletonCubeMeshCpu &mesh);
  void DrawSkinnedMesh(
      const glm::mat4 &mvp, GLuint texture, const GltfPrimitiveCpu &mesh,
      const std::shared_ptr<const std::vector<glm::mat4>> &boneMatrices);
  void DrawBillboard(const glm::mat4 &mvp, GLuint texture,
                     const glm::vec4 &tint);

  /// Local light (x = sky, y = block, both 0..1) for the next draw calls.
  void SetActiveLight(const glm::vec2 &skyBlock) { ActiveLight = skyBlock; }

  const CreatureRenderStats &GetStats() const { return Stats; }
  CreatureDrawQueue &GetDrawQueue() { return Queue; }

private:
  void SubmitCreatureVisual(UWorld &world, UCreature &creature,
                            const CreatureDefinition &def, float dt,
                            const CreatureDrawContext &ctx,
                            const glm::mat4 &viewProj);
  void DrawCreatureHealthBar(const UCreature &creature,
                             const glm::mat4 &viewProj,
                             UGeometryEngine &engine);
  void DrawCreatureDebugBounds(const UCreature &creature, const UWorld &world,
                               const glm::mat4 &viewProj,
                               UGeometryEngine &engine, bool enabled);

  /// Sky/block light at the creature's body, cached per creature, refreshed
  /// at a low rate and eased so cell borders do not flicker. Returns full sky
  /// light when local lighting is off or the cell is not loaded.
  glm::vec2 SampleCreatureLight(UWorld &world, const UCreature &creature,
                                float dt, bool localLight);
  void ApplyCreatureLightUniforms(const UWorld &world, bool enabled);
  void DrawCreatureLabels(UWorld &world, const glm::mat4 &viewProj,
                          UGeometryEngine &engine);

  struct LightState
  {
    glm::vec2 Current{1.0f, 0.0f};
    glm::vec2 Target{1.0f, 0.0f};
    glm::ivec3 Cell{0x7fffffff};
    float SecondsToResample{0.0f};
    uint64_t LastSeenFrame{0};
  };
  struct PendingLabel
  {
    glm::vec3 World;
    std::string Text;
  };

  bool InitPartBuffers();
  bool InitHeadPartBuffers();
  bool InitBodyPartBuffers();
  bool InitRigidHeadPartBuffers();
  bool InitBillboardBuffers();

  GLuint creaturePartVAO{0};
  GLuint creaturePartVBO{0};
  GLuint creaturePartEBO{0};
  GLuint creatureHeadPartVAO{0};
  GLuint creatureHeadPartVBO{0};
  GLuint creatureHeadPartEBO{0};
  GLuint creatureBodyPartVAO{0};
  GLuint creatureBodyPartVBO{0};
  GLuint creatureBodyPartEBO{0};
  GLuint creatureRigidHeadPartVAO{0};
  GLuint creatureRigidHeadPartVBO{0};
  GLuint creatureRigidHeadPartEBO{0};
  GLuint creatureBillboardVAO{0};
  GLuint creatureBillboardVBO{0};
  GLuint creatureBillboardEBO{0};
  std::shared_ptr<UShaderProgram> creatureShader;
  std::shared_ptr<UShaderProgram> creatureSkinnedShader;
  CreatureRenderStats Stats;
  CreatureDrawQueue Queue;
  float ActiveHitFlash01{0.f};
  glm::vec2 ActiveLight{1.0f, 0.0f};
  bool LightingActive{false};
  uint64_t FrameCounter{0};
  std::unordered_map<uint64_t, LightState> LightStates;
  std::vector<PendingLabel> PendingLabels;
  /// Palette uploaded for the current frame (pointer identity): skips the
  /// 4KB UBO re-upload for every primitive of the same creature.
  std::shared_ptr<const std::vector<glm::mat4>> FrameBonePalette;
};

} // namespace cutum

#endif
