#ifndef CREATUREVISUAL_H
#define CREATUREVISUAL_H

#include "Creatures/Core/CreatureCatalogTypes.h"
#include "Creatures/Locomotion/CreatureLocomotionFacts.h"
#include "Creatures/Locomotion/LocomotionTypes.h"
#include "Creatures/Visual/CreatureDrawContext.h"
#include "Pose/CreaturePoseParams.h"
#include <glm/glm.hpp>
#include <memory>

namespace cutum
{

class UCreature;
struct CreatureDefinition;

class IUCreatureVisual
{
public:
  virtual ~IUCreatureVisual() = default;
  virtual void UpdatePose(const UCreature &creature,
                          const CreatureLocomotionFacts &facts,
                          const CreaturePoseParams &pose,
                          const CreatureDefinition &animDef, float dt) = 0;
  virtual void SetAppearance(const ResolvedCreatureAppearance &appearance)
  {
    Appearance = appearance;
    AppearanceSource.reset();
  }
  /// Per-frame entry point: copies only when the cached appearance changed.
  void SetAppearanceShared(
      const std::shared_ptr<const ResolvedCreatureAppearance> &appearance)
  {
    if (appearance && appearance != AppearanceSource)
    {
      SetAppearance(*appearance);
      AppearanceSource = appearance;
    }
  }
  virtual void SubmitDraw(const CreatureDrawContext &ctx,
                          const glm::mat4 &viewProj) = 0;

protected:
  ResolvedCreatureAppearance Appearance{};
  std::shared_ptr<const ResolvedCreatureAppearance> AppearanceSource;
};

} // namespace cutum

#endif
