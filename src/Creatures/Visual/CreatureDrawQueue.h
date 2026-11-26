#ifndef CREATUREDRAWQUEUE_H
#define CREATUREDRAWQUEUE_H

#include "Creatures/Visual/CreatureDrawClipBounds.h"
#include "Creatures/Visual/CreatureDrawRequest.h"
#include "Creatures/Visual/CreatureDrawSink.h"
#include "Items/ItemDrawVisibility.h"
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
  /// Gives every request pushed since `first` the creature's fog factor.
  void StampFog(size_t first, float fog)
  {
    for (size_t i = first; i < Requests.size(); ++i)
    {
      Requests[i].Fog = fog;
    }
  }
  void Flush(ICreatureDrawSink &sink);

  size_t Size() const { return Requests.size(); }
  const std::vector<CreatureDrawRequest> &InspectRequests() const { return Requests; }
  /// Query before Flush sorts the queue; `first` begins one actor's requests.
  bool RangeIntersectsClip(size_t first)const
  {
    if(first>=Requests.size())return true;
    for(size_t i=first;i<Requests.size();++i)
    {
      CreatureDrawClipBounds part;
      part.Add(Requests[i]);
      if(part.Intersects())return true;
    }
    return false;
  }
  void DiscardSince(size_t first)
  {
    if(first<Requests.size())Requests.resize(first);
  }

  /// Known item surfaces use actual world bounds. Unknown request kinds stay
  /// admitted until they supply a world envelope; never substitute a unit box.
  bool ItemRangeIntersectsDistance(size_t first, glm::vec3 camera,
                                   float distance) const
  {
    if (first >= Requests.size())
      return true;
    for (size_t i = first; i < Requests.size(); ++i)
    {
      const auto &request = Requests[i];
      if (request.Kind == CreatureDrawKind::ItemSurface && request.ItemSurface)
      {
        if (ItemIntersectsDrawDistance(*request.ItemSurface, camera, distance))
          return true;
      }
      else if (!request.WorldBounds ||
               BoundsIntersectDrawDistance(*request.WorldBounds, camera,
                                           distance))
        return true;
    }
    return false;
  }

private:
  std::vector<CreatureDrawRequest> Requests;
};

} // namespace cutum

#endif
