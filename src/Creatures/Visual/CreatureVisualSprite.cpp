#include "Creatures/Visual/CreatureVisualSprite.h"

#include "App/Settings/RenderSettings.h"
#include "Creatures/Core/Creature.h"
#include "Creatures/Definition/CreatureDefinition.h"
#include "Creatures/Visual/CreatureTextureResolver.h"
#include "Creatures/Visual/CreatureTextureStorage.h"
#include "Render/Camera/Camera.h"
#include "Render/Engine/GeometryEngine.h"
#include "World/Core/World.h"
#include <cmath>
#include <glm/gtc/matrix_transform.hpp>

namespace cutum
{

std::unique_ptr<IUCreatureVisual> CreateCreatureVisualSprite()
{
  return std::make_unique<UCreatureVisualSprite>();
}

void UCreatureVisualSprite::UpdatePose(
    const UCreature &creature, const CreatureLocomotionFacts & /*facts*/,
    const CreaturePoseParams & /*pose*/, const CreatureDefinition &animDef,
    float /*dt*/)
{
  BodyOrigin = creature.GetFeetPosition();
  SizeBlocks = creature.GetBounds().profile.maxSizeBlocks;
  SpeciesId = animDef.Id;
  TextureStem = animDef.visual.defaultTextureKey.empty()
                    ? Appearance.defaultTextureKey
                    : animDef.visual.defaultTextureKey;
  if (TextureStem.empty())
  {
    TextureStem = "body";
  }
  EmissiveTint = animDef.visual.sprite.emissiveTint;
}

void UCreatureVisualSprite::SubmitDraw(UGeometryEngine &engine,
                                       const glm::mat4 &viewProj)
{
  const RenderSettings &settings = engine.GetRenderSettings();
  auto creatureTextures = engine.GetCreatureTextureStorage();
  if (!creatureTextures || !settings.CreatureTexturedParts)
  {
    return;
  }

  const GLuint tex = ResolveCreatureSpeciesTexture(*creatureTextures, SpeciesId,
                                                   TextureStem, TextureStem);
  if (tex == 0)
  {
    LogCreatureMissingTextureOnce(
        SpeciesId + "|sprite",
        "UCreatureVisualSprite: missing texture species=" + SpeciesId);
    return;
  }

  const glm::vec3 center =
      BodyOrigin + glm::vec3(0.0f, SizeBlocks.y * 0.5f, 0.0f);
  float yawDeg = 0.0f;
  if (const std::shared_ptr<UWorld> world = engine.GetWorld())
  {
    if (auto camera = world->GetCurrentUserCamera())
    {
      const glm::vec3 camPos = camera->GetPosition();
      glm::vec3 toCam = camPos - center;
      toCam.y = 0.0f;
      if (glm::length(toCam) > 0.05f)
      {
        yawDeg = glm::degrees(std::atan2(toCam.x, toCam.z));
      }
    }
  }

  glm::mat4 model = glm::translate(glm::mat4(1.0f), center);
  model = glm::rotate(model, glm::radians(yawDeg), glm::vec3(0.0f, 1.0f, 0.0f));
  model =
      glm::scale(model, glm::vec3(SizeBlocks.x, SizeBlocks.y, SizeBlocks.x));
  engine.DrawCreatureBillboard(viewProj * model, tex, EmissiveTint);
}

} // namespace cutum
