#ifndef CREATUREDRAWQUEUE_H
#define CREATUREDRAWQUEUE_H

#include "Creatures/Visual/CreatureDrawRequest.h"
#include "Creatures/Visual/CreatureDrawSink.h"
#include <algorithm>
#include <vector>

namespace cutum
{

class UGeometryEngine;

/// Queue ordering: stable by draw kind, then texture id, so same-texture
/// requests stay adjacent for batching.
inline bool CreatureDrawRequestLess(const CreatureDrawRequest &a,
                                    const CreatureDrawRequest &b)
{
  if (a.Kind != b.Kind)
  {
    return static_cast<int>(a.Kind) < static_cast<int>(b.Kind);
  }
  return a.Texture < b.Texture;
}

/// Sorts requests into flush order (stable: kind, then texture).
inline void SortCreatureDrawRequests(std::vector<CreatureDrawRequest> &requests)
{
  std::stable_sort(requests.begin(), requests.end(), CreatureDrawRequestLess);
}

class CreatureDrawQueue
{
public:
  void Clear() { Requests.clear(); }
  void Push(CreatureDrawRequest request)
  {
    Requests.push_back(std::move(request));
  }
  /// Gives every request pushed since `first` the creature's local light.
  void StampLight(size_t first, const glm::vec2 &light)
  {
    for (size_t i = first; i < Requests.size(); ++i)
    {
      Requests[i].Light = light;
    }
  }
  void Flush(ICreatureDrawSink &sink);

  size_t Size() const { return Requests.size(); }

private:
  std::vector<CreatureDrawRequest> Requests;
};

} // namespace cutum

#endif
