#include "Render/Effects/InfluenceFxSystem.h"
#include "Creatures/Core/Creature.h"
#include "World/Core/World.h"
#include <algorithm>

namespace cutum
{

namespace
{
UWorld *g_InfluenceFxWorld = nullptr;
} // namespace

void SetInfluenceFxWorld(UWorld *world);

void SetInfluenceFxWorld(UWorld *world) { g_InfluenceFxWorld = world; }

UInfluenceFxSystem &UInfluenceFxSystem::Get()
{
  static UInfluenceFxSystem sys;
  return sys;
}

void UInfluenceFxSystem::RegisterSink()
{
  if (SinkRegistered)
  {
    return;
  }
  InfluenceEvents::AddSink(this);
  SinkRegistered = true;
}

void UInfluenceFxSystem::UnregisterSink()
{
  if (!SinkRegistered)
  {
    return;
  }
  InfluenceEvents::RemoveSink(this);
  SinkRegistered = false;
}

void UInfluenceFxSystem::OnInfluenceEvent(const InfluenceEvent &event)
{
  if (event.Kind != InfluenceEventKind::Applied)
  {
    return;
  }

  if (g_InfluenceFxWorld)
  {
    if (UCreature *src = g_InfluenceFxWorld->GetCreature(event.SourceId))
    {
      src->AddHitFlash(event.Effects.Source.FlashStrength);
    }
    for (CreatureId tid : event.TargetIds)
    {
      if (UCreature *tgt = g_InfluenceFxWorld->GetCreature(tid))
      {
        tgt->AddHitFlash(event.Effects.Target.FlashStrength);
      }
    }
  }

  if (event.Effects.Path.DurationSec > 0.f ||
      event.Effects.Target.FlashStrength > 0.f)
  {
    InfluenceFxBeam beam;
    beam.From = event.SourcePos + glm::vec3(0.f, 1.0f, 0.f);
    beam.To = event.TargetPos + glm::vec3(0.f, 1.0f, 0.f);
    beam.LifeMax = std::max(0.08f, event.Effects.Path.DurationSec > 0.f
                                       ? event.Effects.Path.DurationSec
                                       : 0.18f);
    beam.Life = beam.LifeMax;
    beam.Strength = std::max(event.Effects.Path.FlashStrength,
                             event.Effects.Target.FlashStrength);
    Beams.push_back(beam);
  }

  if (event.Effects.Target.FlashStrength > 0.f)
  {
    InfluenceFxBurst burst;
    burst.Pos = event.TargetPos + glm::vec3(0.f, 1.0f, 0.f);
    burst.Strength = event.Effects.Target.FlashStrength;
    burst.IsSource = false;
    Bursts.push_back(burst);
  }
  if (event.Effects.Source.FlashStrength > 0.f)
  {
    InfluenceFxBurst burst;
    burst.Pos = event.SourcePos + glm::vec3(0.f, 1.0f, 0.f);
    burst.Strength = event.Effects.Source.FlashStrength;
    burst.IsSource = true;
    Bursts.push_back(burst);
  }
}

void UInfluenceFxSystem::Update(float dt)
{
  if (dt <= 0.f)
  {
    return;
  }
  for (size_t i = 0; i < Beams.size();)
  {
    Beams[i].Life -= dt;
    if (Beams[i].Life <= 0.f)
    {
      Beams.erase(Beams.begin() + static_cast<std::ptrdiff_t>(i));
      continue;
    }
    ++i;
  }
  for (size_t i = 0; i < Bursts.size();)
  {
    Bursts[i].Life -= dt;
    if (Bursts[i].Life <= 0.f)
    {
      Bursts.erase(Bursts.begin() + static_cast<std::ptrdiff_t>(i));
      continue;
    }
    ++i;
  }
}

} // namespace cutum
