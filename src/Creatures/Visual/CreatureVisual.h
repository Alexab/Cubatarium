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
  /// Conservative world visual envelope, independent of collision. May load
  /// a cached asset, but must not advance a pose/clock. False requests posed
  /// queue clipping instead; nonfinite bounds explicitly disable rejection.
  virtual bool GetConservativeWorldBounds(const UCreature &,
      const CreatureDefinition &,glm::vec3 &,glm::vec3 &){return false;}
  /// Optional attachment anchor from the same cached pose/root used to draw
  /// the body. Consumers pass identity as bodyMat when using this full matrix.
  virtual bool GetWorldBoneTransform(const std::string &, glm::mat4 &) const
  {
    return false;
  }
  /// Rest geometry envelope expressed in the attachment bone's local space.
  virtual bool GetBoneLocalBounds(const std::string &, glm::vec3 &, glm::vec3 &) const
  {
    return false;
  }

protected:
  ResolvedCreatureAppearance Appearance{};
  std::shared_ptr<const ResolvedCreatureAppearance> AppearanceSource;
};

} // namespace cutum

#endif
