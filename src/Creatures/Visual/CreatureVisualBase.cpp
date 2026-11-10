#include "Creatures/Visual/CreatureVisualBase.h"

#include "App/Settings/RenderSettings.h"
#include "Creatures/Core/Creature.h"
#include "Creatures/Definition/CreatureDefinition.h"

namespace cutum
{

float UCreatureVisualBase::ComputeBodyYawDeg(const UCreature &creature,
                                            const CreatureDefinition &animDef) const
{
  return creature.GetYaw() + animDef.visual.modelYawOffsetDeg;
}

void UCreatureVisualBase::RefreshDefaultTextureKey(
    const CreatureDefinition &animDef)
{
  DefaultTextureKey = animDef.visual.defaultTextureKey.empty()
                          ? Appearance.defaultTextureKey
                          : animDef.visual.defaultTextureKey;
  if (DefaultTextureKey.empty())
  {
    DefaultTextureKey = "body";
  }
}

bool UCreatureVisualBase::DrawEnabled(const CreatureDrawContext &ctx) const
{
  return ctx.Queue != nullptr && ctx.Textures != nullptr &&
         ctx.Settings != nullptr && ctx.Settings->CreatureTexturedParts;
}

} // namespace cutum
