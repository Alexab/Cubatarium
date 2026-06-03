#include "CreatureVisualRigid.h"
#include "Creature.h"
#include "CreatureBounds.h"
#include "CreatureDefinition.h"
#include "CreaturePartMeshData.h"
#include "CreatureTextureStorage.h"
#include "GeometryEngine.h"
#include <glm/gtc/matrix_transform.hpp>
#include <iostream>

namespace cutum {

namespace {

CreaturePartMesh MeshForPart(const ResolvedCreaturePart& part)
{
 if (part.partId == "head") {
  return CreaturePartMesh::Head;
 }
 if (part.partId == "torso") {
  return CreaturePartMesh::Body;
 }
 return CreaturePartMesh::Box;
}

} // namespace

glm::mat4 CreatureVisualRigid::BuildPartModel(const glm::vec3& bodyOrigin, float bodyYaw,
                                              const glm::vec3& localOffset,
                                              const glm::vec3& sizeBlocks)
{
 glm::mat4 model = glm::translate(glm::mat4(1.0f), bodyOrigin);
 model = glm::rotate(model, glm::radians(bodyYaw), glm::vec3(0.0f, 1.0f, 0.0f));
 model = glm::translate(model, localOffset);
 model = glm::scale(model, sizeBlocks);
 return model;
}

void CreatureVisualRigid::UpdatePose(const Creature& creature, LocomotionState state,
                                     const CreatureDefinition& /*animDef*/, float /*dt*/)
{
 bodyOrigin_ = creature.GetBodyOrigin();
 bodyYaw_ = creature.GetYaw();
 sizeBlocks_ = creature.GetBounds().currentSizeBlocks;
 switch (state) {
 case LocomotionState::Walk:
  headYaw_ = 0.2f;
  break;
 case LocomotionState::Fly:
  headYaw_ = 0.1f;
  break;
 default:
  headYaw_ = 0.0f;
  break;
 }
}

void CreatureVisualRigid::SubmitDraw(GeometryEngine& engine, const glm::mat4& viewProj)
{
 const RenderSettings& settings = engine.GetRenderSettings();
 const bool drawTextured = settings.creatureTexturedParts && !appearance_.parts.empty();
 auto creatureTextures = engine.GetCreatureTextureStorage();

 if (drawTextured && creatureTextures) {
  for (const ResolvedCreaturePart& part : appearance_.parts) {
   glm::vec3 offset = part.offsetBlocks;
   if (part.partId == "head") {
    offset.z += headYaw_ * 0.15f;
   }
   const glm::mat4 model = BuildPartModel(bodyOrigin_, bodyYaw_, offset, part.sizeBlocks);
   const GLuint tex = creatureTextures->GetTexture(part.textureAssetKey);
   if (tex != 0) {
    engine.DrawCreatureTexturedPart(viewProj * model, tex, MeshForPart(part));
   } else if (settings.creatureWireframeOverlay) {
    engine.DrawBoxWireframe(viewProj * model, appearance_.wireframeColor);
   } else {
    static std::string lastKey;
    if (lastKey != part.textureAssetKey) {
     std::cerr << "CreatureVisualRigid: missing texture " << part.textureAssetKey << std::endl;
     lastKey = part.textureAssetKey;
    }
   }
  }
 } else if (appearance_.useWireframeFallback || !drawTextured) {
  const glm::vec3 localCenter(0.0f, sizeBlocks_.y * 0.5f, 0.0f);
  const glm::mat4 model = BuildPartModel(bodyOrigin_, bodyYaw_, localCenter, sizeBlocks_);
  engine.DrawBoxWireframe(viewProj * model, appearance_.wireframeColor);
 }

 if (settings.creatureWireframeOverlay && drawTextured) {
  for (const ResolvedCreaturePart& part : appearance_.parts) {
   glm::vec3 offset = part.offsetBlocks;
   if (part.partId == "head") {
    offset.z += headYaw_ * 0.15f;
   }
   const glm::mat4 model = BuildPartModel(bodyOrigin_, bodyYaw_, offset, part.sizeBlocks);
   engine.DrawBoxWireframe(viewProj * model, appearance_.wireframeColor);
  }
 }
}

} // namespace cutum
