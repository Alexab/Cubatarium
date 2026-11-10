#ifndef INFLUENCE_EVENT_H
#define INFLUENCE_EVENT_H

#include "Creatures/Influence/EffectSpec.h"
#include "Creatures/Influence/InfluenceTypes.h"
#include <cstdint>
#include <glm/glm.hpp>
#include <string>
#include <vector>

namespace cutum
{

using CreatureId = uint64_t;

enum class InfluenceEventKind
{
  Applied = 0,
  Cancelled,
  DigProgress
};

struct InfluenceEvent
{
  InfluenceEventKind Kind{InfluenceEventKind::Applied};
  CreatureId SourceId{0};
  std::vector<CreatureId> TargetIds;
  std::string CapabilityId;
  InfluenceChannel Channel{InfluenceChannel::None};
  float DamageDealt{0.f};
  float DigProgress{0.f};
  glm::ivec3 DigBlockPos{0};
  glm::vec3 SourcePos{0.f};
  glm::vec3 TargetPos{0.f};
  EffectSpec Effects;
  std::string CancelReason;
};

class IUInfluenceEventSink
{
public:
  virtual ~IUInfluenceEventSink() = default;
  virtual void OnInfluenceEvent(const InfluenceEvent &event) = 0;
};

struct InfluenceEvents
{
  static void AddSink(IUInfluenceEventSink *sink);
  static void RemoveSink(IUInfluenceEventSink *sink);
  static void Emit(const InfluenceEvent &event);
};

} // namespace cutum

#endif
