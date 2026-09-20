#pragma once

#include <cstdint>

namespace cutum
{

/// Single Replace/Remove/Retain decision for greedy GPU publish (sysreset).
enum class MeshPublishAction : uint8_t
{
  Retain = 0,
  Replace = 1,
  Remove = 2,
  PublishedEmpty = 3,
};

struct MeshPublishRevs
{
  uint64_t geom_rev{0};
  uint64_t light_rev{0};
  uint64_t material_stamp{0};
};

inline bool MeshPublishRevsMatch(const MeshPublishRevs &got,
                                const MeshPublishRevs &expected)
{
  return got.geom_rev == expected.geom_rev &&
         got.light_rev == expected.light_rev &&
         got.material_stamp == expected.material_stamp;
}

/// Accept only when (geom, light, material) match expected — else Retain prior.
inline bool ShouldAcceptMeshPublish(const MeshPublishRevs &got,
                                    const MeshPublishRevs &expected,
                                    bool gpu_ready)
{
  return gpu_ready && MeshPublishRevsMatch(got, expected);
}

/// Empty intentional publish is PublishedEmpty (not a silent Retain forever).
inline MeshPublishAction DecideMeshPublishAction(bool accept,
                                                 bool new_drawable,
                                                 bool had_prior_drawable)
{
  if (!accept)
  {
    return MeshPublishAction::Retain;
  }
  if (!new_drawable)
  {
    return had_prior_drawable ? MeshPublishAction::PublishedEmpty
                              : MeshPublishAction::Remove;
  }
  return MeshPublishAction::Replace;
}

/// Live∩Free must be empty: cannot free a slot that is still the live draw.
inline bool MeshPublishLiveFreeDisjoint(bool is_live_draw, bool freeing_slot)
{
  return !(is_live_draw && freeing_slot);
}

} // namespace cutum
