#include "Creatures/Influence/InfluenceEvent.h"
#include <algorithm>
#include <vector>

namespace cutum
{

namespace
{
std::vector<IUInfluenceEventSink *> &Sinks()
{
  static std::vector<IUInfluenceEventSink *> sinks;
  return sinks;
}
} // namespace

void InfluenceEvents::AddSink(IUInfluenceEventSink *sink)
{
  if (!sink)
  {
    return;
  }
  auto &sinks = Sinks();
  if (std::find(sinks.begin(), sinks.end(), sink) == sinks.end())
  {
    sinks.push_back(sink);
  }
}

void InfluenceEvents::RemoveSink(IUInfluenceEventSink *sink)
{
  auto &sinks = Sinks();
  sinks.erase(std::remove(sinks.begin(), sinks.end(), sink), sinks.end());
}

void InfluenceEvents::Emit(const InfluenceEvent &event)
{
  for (IUInfluenceEventSink *sink : Sinks())
  {
    if (sink)
    {
      sink->OnInfluenceEvent(event);
    }
  }
}

} // namespace cutum
