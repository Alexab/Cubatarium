#include "Creatures/Visual/CreatureVisualRigid.h"
#include "Creatures/Core/Creature.h"
#include "Creatures/Core/CreatureBounds.h"
#include "Creatures/Definition/CreatureDefinition.h"
#include "Creatures/Visual/CreaturePartMeshData.h"
#include "Creatures/Visual/CreatureTextureStorage.h"
#include "Render/Engine/GeometryEngine.h"
#include <glm/gtc/matrix_transform.hpp>
#include <optional>

namespace cutum
{

namespace
{

bool IsLegLimb(const ResolvedCreaturePart &part)
{
  if (part.LimbKind == "leg")
  {
    return true;
  }
  const std::string &id = part.partId;
  return id == "leg_l" || id == "leg_r" || id == "leg_fl" || id == "leg_fr" ||
         id == "leg_bl" || id == "leg_br";
}

bool IsArmLimb(const ResolvedCreaturePart &part)
{
  if (part.LimbKind == "arm")
  {
    return true;
  }
  const std::string &id = part.partId;
  return id == "arm_l" || id == "arm_r";
}

float CrouchUpperBodyDrop(const UCreature &creature, float stanceBlend01)
{
  if (stanceBlend01 <= 0.0f)
  {
    return 0.0f;
  }
  return stanceBlend01 * (creature.GetEyeOffset().y -
                          creature.GetLocomotion().GetViewEyeHeight());
}

CreaturePartMesh MeshForPart(const ResolvedCreaturePart &part,
                             CreatureTextureLayout layout)
{
  if (layout == CreatureTextureLayout::PlayerSkinAtlas)
  {
    if (part.partId == "head")
    {
      return CreaturePartMesh::Head;
    }
    if (part.partId == "torso")
    {
      return CreaturePartMesh::Body;
    }
  }
  else if (layout == CreatureTextureLayout::RigidCrop &&
           UsesRigidFaceTexture(part.textureAssetKey))
  {
    return CreaturePartMesh::RigidHead;
  }
  return CreaturePartMesh::Box;
}

std::optional<float>
TorsoBottomY(const std::vector<ResolvedCreaturePart> &Parts)
{
  for (const ResolvedCreaturePart &part : Parts)
  {
    if (part.partId == "torso")
    {
      return part.offsetBlocks.y - part.sizeBlocks.y * 0.5f;
    }
  }
  return std::nullopt;
}

glm::mat4 BuildRigidPartModel(const glm::vec3 &bodyOrigin, float bodyYaw,
                              const glm::vec3 &localOffset,
                              const glm::vec3 &sizeBlocks,
                              const CreaturePartPose *partPose)
{
  glm::mat4 model = glm::translate(glm::mat4(1.0f), bodyOrigin);
  model =
      glm::rotate(model, glm::radians(bodyYaw), glm::vec3(0.0f, 1.0f, 0.0f));
  glm::vec3 offset = localOffset;
  if (partPose)
  {
    offset += partPose->offsetDelta;
  }
  model = glm::translate(model, offset);
  if (partPose)
  {
    model = glm::rotate(model, glm::radians(partPose->eulerDeg.x),
                        glm::vec3(1.0f, 0.0f, 0.0f));
    model = glm::rotate(model, glm::radians(partPose->eulerDeg.y),
                        glm::vec3(0.0f, 1.0f, 0.0f));
    model = glm::rotate(model, glm::radians(partPose->eulerDeg.z),
                        glm::vec3(0.0f, 0.0f, 1.0f));
  }
  model = glm::scale(model, sizeBlocks);
  return model;
}

glm::mat4 BuildLimbPartModel(const glm::vec3 &bodyOrigin, float bodyYaw,
                             const glm::vec3 &pivot,
                             const glm::vec3 &meshCenter,
                             const glm::vec3 &sizeBlocks,
                             const CreaturePartPose *partPose)
{
  glm::mat4 model = glm::translate(glm::mat4(1.0f), bodyOrigin);
  model =
      glm::rotate(model, glm::radians(bodyYaw), glm::vec3(0.0f, 1.0f, 0.0f));
  glm::vec3 pivotPos = pivot;
  if (partPose)
  {
    pivotPos += partPose->offsetDelta;
  }
  model = glm::translate(model, pivotPos);
  if (partPose)
  {
    model = glm::rotate(model, glm::radians(partPose->eulerDeg.x),
                        glm::vec3(1.0f, 0.0f, 0.0f));
    model = glm::rotate(model, glm::radians(partPose->eulerDeg.y),
                        glm::vec3(0.0f, 1.0f, 0.0f));
    model = glm::rotate(model, glm::radians(partPose->eulerDeg.z),
                        glm::vec3(0.0f, 0.0f, 1.0f));
  }
  model = glm::translate(model, meshCenter - pivot);
  model = glm::scale(model, sizeBlocks);
  return model;
}

glm::vec3 DefaultLegPivot(const ResolvedCreaturePart &part,
                          float torsoBottomY)
{
  return glm::vec3(part.offsetBlocks.x, torsoBottomY, part.offsetBlocks.z);
}

glm::vec3 DefaultArmPivot(const ResolvedCreaturePart &part)
{
  return glm::vec3(part.offsetBlocks.x,
                   part.offsetBlocks.y + part.sizeBlocks.y * 0.5f,
                   part.offsetBlocks.z);
}

} // namespace

void UCreatureVisualRigid::UpdatePose(const UCreature &creature,
                                      const CreatureLocomotionFacts & /*facts*/,
                                      const CreaturePoseParams &pose,
                                      const CreatureDefinition & /*animDef*/,
                                      float /*dt*/)
{
  BodyOrigin = creature.GetFeetPosition();
  BodyYaw = creature.GetYaw();
  const float blend = pose.crouchUpperDrop > 0.0f
                          ? pose.crouchUpperDrop
                          : creature.GetLocomotion().GetStanceBlend();
  CrouchUpperDrop = CrouchUpperBodyDrop(creature, blend);
  SizeBlocks = creature.GetBounds().currentSizeBlocks;
  PartPoses = pose.Parts;
}

void UCreatureVisualRigid::SubmitDraw(UGeometryEngine &engine,
                                      const glm::mat4 &viewProj)
{
  const RenderSettings &settings = engine.GetRenderSettings();
  const bool drawTextured =
      settings.CreatureTexturedParts && !Appearance.Parts.empty();
  auto creatureTextures = engine.GetCreatureTextureStorage();
  const std::optional<float> torsoBottomY = TorsoBottomY(Appearance.Parts);

  auto drawPart = [&](const ResolvedCreaturePart &part)
  {
    glm::vec3 offset = part.offsetBlocks;
    if (!IsLegLimb(part))
    {
      offset.y -= CrouchUpperDrop;
    }
    const CreaturePartPose *partPose = nullptr;
    if (const auto it = PartPoses.find(part.partId); it != PartPoses.end())
    {
      partPose = &it->second;
    }

    if (part.HasPivot)
    {
      return BuildLimbPartModel(BodyOrigin, BodyYaw, part.PivotBlocks,
                                part.offsetBlocks, part.sizeBlocks, partPose);
    }
    if (IsLegLimb(part) && torsoBottomY)
    {
      const glm::vec3 pivot = DefaultLegPivot(part, *torsoBottomY);
      return BuildLimbPartModel(BodyOrigin, BodyYaw, pivot, part.offsetBlocks,
                                part.sizeBlocks, partPose);
    }
    if (IsArmLimb(part))
    {
      const glm::vec3 pivot = DefaultArmPivot(part);
      return BuildLimbPartModel(BodyOrigin, BodyYaw, pivot, part.offsetBlocks,
                                part.sizeBlocks, partPose);
    }
    return BuildRigidPartModel(BodyOrigin, BodyYaw, offset, part.sizeBlocks,
                               partPose);
  };

  const CreatureTextureLayout textureLayout =
      ParseCreatureTextureLayout(Appearance.textureLayout);

  if (drawTextured && creatureTextures)
  {
    for (const ResolvedCreaturePart &part : Appearance.Parts)
    {
      const glm::mat4 model = drawPart(part);
      const GLuint tex = creatureTextures->GetTexture(part.textureAssetKey);
      if (tex != 0)
      {
        engine.DrawCreatureTexturedPart(viewProj * model, tex,
                                        MeshForPart(part, textureLayout));
      }
      else if (settings.CreatureWireframeOverlay)
      {
        engine.DrawBoxWireframe(viewProj * model, Appearance.wireframeColor);
      }
    }
  }
  else if (Appearance.useWireframeFallback || !drawTextured)
  {
    const glm::vec3 localCenter(0.0f, SizeBlocks.y * 0.5f, 0.0f);
    const glm::mat4 model = BuildRigidPartModel(
        BodyOrigin, BodyYaw, localCenter, SizeBlocks, nullptr);
    engine.DrawBoxWireframe(viewProj * model, Appearance.wireframeColor);
  }

  if (settings.CreatureWireframeOverlay && drawTextured)
  {
    for (const ResolvedCreaturePart &part : Appearance.Parts)
    {
      engine.DrawBoxWireframe(viewProj * drawPart(part),
                              Appearance.wireframeColor);
    }
  }
}

} // namespace cutum
