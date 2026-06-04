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

bool IsLegPart(const std::string& partId)
{
 return partId == "leg_l" || partId == "leg_r";
}

float CrouchUpperBodyDrop(const Creature& creature, float stanceBlend01)
{
 if (stanceBlend01 <= 0.0f) {
  return 0.0f;
 }
 return stanceBlend01 * (creature.GetEyeOffset().y - creature.GetLocomotion().GetViewEyeHeight());
}

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

glm::mat4 BuildPartModel(const glm::vec3& bodyOrigin, float bodyYaw, const glm::vec3& localOffset,
                         const glm::vec3& sizeBlocks, const CreaturePartPose* partPose)
{
 glm::mat4 model = glm::translate(glm::mat4(1.0f), bodyOrigin);
 model = glm::rotate(model, glm::radians(bodyYaw), glm::vec3(0.0f, 1.0f, 0.0f));
 glm::vec3 offset = localOffset;
 if (partPose) {
  offset += partPose->offsetDelta;
 }
 model = glm::translate(model, offset);
 if (partPose) {
  model = glm::rotate(model, glm::radians(partPose->eulerDeg.x), glm::vec3(1.0f, 0.0f, 0.0f));
  model = glm::rotate(model, glm::radians(partPose->eulerDeg.y), glm::vec3(0.0f, 1.0f, 0.0f));
  model = glm::rotate(model, glm::radians(partPose->eulerDeg.z), glm::vec3(0.0f, 0.0f, 1.0f));
 }
 model = glm::scale(model, sizeBlocks);
 return model;
}

} // namespace

void CreatureVisualRigid::UpdatePose(const Creature& creature, const CreatureLocomotionFacts& /*facts*/,
                                   const CreaturePoseParams& pose, const CreatureDefinition& /*animDef*/,
                                   float /*dt*/)
{
 bodyOrigin_ = creature.GetFeetPosition();
 bodyYaw_ = creature.GetYaw();
 const float blend = pose.crouchUpperDrop > 0.0f ? pose.crouchUpperDrop : creature.GetLocomotion().GetStanceBlend();
 crouchUpperDrop_ = CrouchUpperBodyDrop(creature, blend);
 sizeBlocks_ = creature.GetBounds().currentSizeBlocks;
 partPoses_ = pose.parts;
 headYaw_ = 0.0f;
 if (const auto it = partPoses_.find("head"); it != partPoses_.end()) {
  headYaw_ = it->second.offsetDelta.z;
 }
}

void CreatureVisualRigid::SubmitDraw(GeometryEngine& engine, const glm::mat4& viewProj)
{
 const RenderSettings& settings = engine.GetRenderSettings();
 const bool drawTextured = settings.creatureTexturedParts && !appearance_.parts.empty();
 auto creatureTextures = engine.GetCreatureTextureStorage();

 auto drawPart = [&](const ResolvedCreaturePart& part) {
  glm::vec3 offset = part.offsetBlocks;
  if (!IsLegPart(part.partId)) {
   offset.y -= crouchUpperDrop_;
  }
  const CreaturePartPose* partPose = nullptr;
  if (const auto it = partPoses_.find(part.partId); it != partPoses_.end()) {
   partPose = &it->second;
   if (part.partId == "head" && headYaw_ != 0.0f) {
    offset.z += headYaw_ * 0.15f;
   }
  }
  return BuildPartModel(bodyOrigin_, bodyYaw_, offset, part.sizeBlocks, partPose);
 };

 if (drawTextured && creatureTextures) {
  for (const ResolvedCreaturePart& part : appearance_.parts) {
   const glm::mat4 model = drawPart(part);
   const GLuint tex = creatureTextures->GetTexture(part.textureAssetKey);
   if (tex != 0) {
    engine.DrawCreatureTexturedPart(viewProj * model, tex, MeshForPart(part));
   } else if (settings.creatureWireframeOverlay) {
    engine.DrawBoxWireframe(viewProj * model, appearance_.wireframeColor);
   }
  }
 } else if (appearance_.useWireframeFallback || !drawTextured) {
  const glm::vec3 localCenter(0.0f, sizeBlocks_.y * 0.5f, 0.0f);
  const glm::mat4 model = BuildPartModel(bodyOrigin_, bodyYaw_, localCenter, sizeBlocks_, nullptr);
  engine.DrawBoxWireframe(viewProj * model, appearance_.wireframeColor);
 }

 if (settings.creatureWireframeOverlay && drawTextured) {
  for (const ResolvedCreaturePart& part : appearance_.parts) {
   engine.DrawBoxWireframe(viewProj * drawPart(part), appearance_.wireframeColor);
  }
 }
}

} // namespace cutum
