#include "CreatureVisualRigid.h"
#include "Creature.h"
#include "CreatureBounds.h"
#include "CreatureDefinition.h"
#include "CreatureTextureStorage.h"
#include "GeometryEngine.h"
#include <glm/gtc/matrix_transform.hpp>
#include <iostream>

namespace cutum {


void CreatureVisualRigid::UpdatePose(const Creature& creature, LocomotionState state,
                                     const CreatureDefinition& /*animDef*/, float /*dt*/)
{
 bodyOrigin_ = creature.GetBodyOrigin();
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
    offset.x += headYaw_ * 0.15f;
   }
   const glm::vec3 center = bodyOrigin_ + offset;
   glm::mat4 model = glm::translate(glm::mat4(1.0f), center);
   model = glm::scale(model, part.sizeBlocks);
   const GLuint tex = creatureTextures->GetTexture(part.textureAssetKey);
   if (tex != 0) {
    engine.DrawCreatureTexturedPart(viewProj * model, tex);
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
  const glm::vec3 center = BoundsCollisionCenter(bodyOrigin_, sizeBlocks_);
  glm::mat4 model = glm::translate(glm::mat4(1.0f), center);
  model = glm::scale(model, sizeBlocks_);
  engine.DrawBoxWireframe(viewProj * model, appearance_.wireframeColor);
 }

 if (settings.creatureWireframeOverlay && drawTextured) {
  for (const ResolvedCreaturePart& part : appearance_.parts) {
   glm::vec3 offset = part.offsetBlocks;
   if (part.partId == "head") {
    offset.x += headYaw_ * 0.15f;
   }
   const glm::vec3 center = bodyOrigin_ + offset;
   glm::mat4 model = glm::translate(glm::mat4(1.0f), center);
   model = glm::scale(model, part.sizeBlocks);
   engine.DrawBoxWireframe(viewProj * model, appearance_.wireframeColor);
  }
 }
}

} // namespace cutum
