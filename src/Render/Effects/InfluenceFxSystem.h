#ifndef INFLUENCE_FX_SYSTEM_H
#define INFLUENCE_FX_SYSTEM_H

#include "Creatures/Influence/InfluenceEvent.h"
#include <glm/glm.hpp>
#include <vector>

namespace cutum
{

struct InfluenceFxBeam
{
  glm::vec3 From{0.f};
  glm::vec3 To{0.f};
  float Life{0.2f};
  float LifeMax{0.2f};
  float Strength{1.f};
};

struct InfluenceFxBurst
{
  glm::vec3 Pos{0.f};
  float Life{0.35f};
  float LifeMax{0.35f};
  float Strength{1.f};
  bool IsSource{false};
};

/// Presentation-only FX driven by InfluenceEvents (no combat math).
class UInfluenceFxSystem : public IUInfluenceEventSink
{
public:
  static UInfluenceFxSystem &Get();

  void RegisterSink();
  void UnregisterSink();

  void OnInfluenceEvent(const InfluenceEvent &event) override;
  void Update(float dt);

  const std::vector<InfluenceFxBeam> &GetBeams() const { return Beams; }
  const std::vector<InfluenceFxBurst> &GetBursts() const { return Bursts; }

private:
  UInfluenceFxSystem() = default;
  bool SinkRegistered{false};
  std::vector<InfluenceFxBeam> Beams;
  std::vector<InfluenceFxBurst> Bursts;
};

class UWorld;
void SetInfluenceFxWorld(UWorld *world);

} // namespace cutum

#endif
