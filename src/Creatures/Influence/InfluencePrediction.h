#ifndef INFLUENCE_PREDICTION_H
#define INFLUENCE_PREDICTION_H

#include "Creatures/Influence/InfluenceCapability.h"
#include "Creatures/Influence/InfluenceTypes.h"
#include <cstdint>
#include <glm/glm.hpp>
#include <string>
#include <vector>

namespace cutum
{

using CreatureId = uint64_t;

struct InfluenceTargetDelta
{
  CreatureId TargetId{0};
  float HealthDelta{0.f};
  float FatigueDelta{0.f};
  std::vector<std::string> StatusIdsToAdd;
  glm::vec3 TargetPos{0.f};
};

struct InfluencePrediction
{
  bool Valid{false};
  bool Cancelled{false};
  std::string CancelReason;
  CreatureId SourceId{0};
  InfluenceCapability Capability;
  float SourceFatigueDelta{0.f};
  float IntervalMul{1.f};
  glm::vec3 SourcePos{0.f};
  std::vector<InfluenceTargetDelta> Targets;
  glm::ivec3 DigBlockPos{0};
  float DigDurationSec{-1.f};
  float DigWearDelta{0.f};
  bool DigEffective{false};
  std::string DigToolId;
};

} // namespace cutum

#endif
