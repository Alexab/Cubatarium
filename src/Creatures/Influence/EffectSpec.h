#ifndef EFFECT_SPEC_H
#define EFFECT_SPEC_H

#include <string>

namespace cutum
{

/// Data-driven presentation hooks for an influence (no Render includes).
struct EffectNodeSpec
{
  std::string ParticleId;
  std::string SoundId;
  float FlashStrength{0.f};
  float DurationSec{0.25f};
};

struct EffectSpec
{
  EffectNodeSpec Source;
  EffectNodeSpec Target;
  EffectNodeSpec Path;
  std::string PathStyle{"line"};
};

} // namespace cutum

#endif
