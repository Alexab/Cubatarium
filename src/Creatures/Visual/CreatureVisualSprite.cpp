#include "Creatures/Visual/CreatureVisualSprite.h"

#include "Creatures/Core/Creature.h"
#include "Creatures/Definition/CreatureDefinition.h"
#include "Creatures/Visual/CreatureDrawQueue.h"
#include "Creatures/Visual/CreatureDrawRequest.h"
#include "Creatures/Visual/CreatureTextureResolver.h"
#include "Creatures/Visual/CreatureTextureStorage.h"
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

void UCreatureVisualSprite::SubmitDraw(const CreatureDrawContext &ctx,
                                       const glm::mat4 &viewProj)
{
  if (!DrawEnabled(ctx))
  {
    return;
  }

  const GLuint tex = ResolveCreatureSpeciesTexture(*ctx.Textures, SpeciesId,
                                                   TextureStem, TextureStem);
  if (tex == 0)
  {
    LogCreatureMissingTextureOnce(
        SpeciesId + "|sprite",
        "UCreatureVisualSprite: missing texture species=" + SpeciesId);
    return;
  }

  // Face the camera using the position from the draw context — no more
  // Visual -> World -> Camera reach-through (audit V9) — and submit through
  // the draw queue instead of drawing immediately between sorted batches.
  const glm::vec3 center =
      BodyOrigin + glm::vec3(0.0f, SizeBlocks.y * 0.5f, 0.0f);
  float yawDeg = 0.0f;
  glm::vec3 toCam = ctx.CameraPosition - center;
  toCam.y = 0.0f;
  if (glm::length(toCam) > 0.05f)
  {
    yawDeg = glm::degrees(std::atan2(toCam.x, toCam.z));
  }

  glm::mat4 model = glm::translate(glm::mat4(1.0f), center);
  model = glm::rotate(model, glm::radians(yawDeg), glm::vec3(0.0f, 1.0f, 0.0f));
  model =
      glm::scale(model, glm::vec3(SizeBlocks.x, SizeBlocks.y, SizeBlocks.x));

  CreatureDrawRequest req;
  req.Kind = CreatureDrawKind::Billboard;
  req.Mvp = viewProj * model;
  req.Texture = tex;
  req.Tint = EmissiveTint;
  ctx.Queue->Push(std::move(req));
}

} // namespace cutum
