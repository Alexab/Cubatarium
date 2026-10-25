#ifndef CREATUREVISUALBASE_H
#define CREATUREVISUALBASE_H

#include "Creatures/Visual/CreatureVisual.h"

#include <glm/glm.hpp>
#include <string>

namespace cutum
{

class UCreature;
struct CreatureDefinition;

/// Shared plumbing for visual backends: per-frame body state, one unified
/// yaw source, default texture key resolution, and the draw guard
/// (audit 2.1 — this logic was copy-pasted across all four backends).
class UCreatureVisualBase : public IUCreatureVisual
{
protected:
  glm::vec3 BodyOrigin{0.f};
  float BodyYawDeg{0.f};
  std::string SpeciesId;
  std::string DefaultTextureKey;

  /// Creature heading plus the definition's model offset. Rigid used to read
  /// the creature's copy of the offset while other backends read the
  /// definition's — same value, two sources (audit V1).
  float ComputeBodyYawDeg(const UCreature &creature,
                          const CreatureDefinition &animDef) const;

  /// Definition default, else appearance default, else "body".
  void RefreshDefaultTextureKey(const CreatureDefinition &animDef);

  /// True when the context has everything needed to submit textured draws.
  bool DrawEnabled(const CreatureDrawContext &ctx) const;
};

} // namespace cutum

#endif
