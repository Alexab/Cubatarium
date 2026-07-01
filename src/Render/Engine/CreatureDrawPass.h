#ifndef CREATUREDRAWPASS_H
#define CREATUREDRAWPASS_H

#include "App/Settings/RenderSettings.h"
#include "Creatures/Visual/CreatureDrawQueue.h"
#include "Creatures/Visual/CreaturePartMeshData.h"
#include "Creatures/Visual/CreatureRenderStats.h"
#include "Creatures/Visual/Gltf/CreatureGltfTypes.h"
#include "Render/Engine/ShaderManager.h"
#include <glm/glm.hpp>
#include <memory>
#include <vector>

typedef unsigned int GLuint;

namespace cutum
{

class UCreatureTextureStorage;
class UGeometryEngine;
class UShaderProgram;
class UWorld;

class UCreatureDrawPass
{
public:
  bool InitBuffers(UShaderManager &shader_manager);
  void DestroyBuffers();

  void Render(UWorld &world, UGeometryEngine &engine, const RenderSettings &render);

  void DrawTexturedPart(const glm::mat4 &mvp, GLuint texture,
                        CreaturePartMesh mesh = CreaturePartMesh::Box);
  void DrawBoneSkeletonMesh(const glm::mat4 &mvp, GLuint texture,
                            const BoneSkeletonCubeMeshCpu &mesh);
  void DrawGltfMesh(const glm::mat4 &mvp, GLuint texture,
                    const BoneSkeletonCubeMeshCpu &mesh);
  void DrawSkinnedMesh(const glm::mat4 &mvp, GLuint texture,
                       const GltfPrimitiveCpu &mesh,
                       const std::vector<glm::mat4> &boneMatrices);

  const CreatureRenderStats &GetStats() const { return Stats; }
  CreatureDrawQueue &GetDrawQueue() { return Queue; }

private:
  bool InitPartBuffers();
  bool InitHeadPartBuffers();
  bool InitBodyPartBuffers();
  bool InitRigidHeadPartBuffers();

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
  std::shared_ptr<UShaderProgram> creatureShader;
  std::shared_ptr<UShaderProgram> creatureSkinnedShader;
  CreatureRenderStats Stats;
  CreatureDrawQueue Queue;
};

} // namespace cutum

#endif
