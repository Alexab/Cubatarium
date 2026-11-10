#ifndef CREATURE_LIGHT_CACHE_H
#define CREATURE_LIGHT_CACHE_H

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <glm/glm.hpp>
#include <iterator>
#include <unordered_map>

namespace cutum
{

/// Smoothed per-creature light (sky, block) for the creature draw pass.
/// Keyed by creature id, which restarts at 1 when a world is loaded or its
/// creatures are cleared: the cache is tied to the world's creature
/// generation and dropped when it changes, so a new creature never inherits
/// the light of an old one with the same id.
/// Block cell whose light a creature takes: the cell holding the body centre
/// (blocks are centred on integers, so floor(x + 0.5)), or the one above it
/// when that cell is solid. The draw pass used floor(x): the centre of a low
/// body (tortoise, centre ~0.2 above the ground) landed in the ground block,
/// and horizontally in the neighbouring cell half the time; solid cells have
/// light 0, so such creatures were drawn black, flipping as they walked past
/// trunks and slopes.
template <typename IsSolid>
glm::ivec3 CreatureLightSampleCell(const glm::vec3 &bodyCenter,
                                   const IsSolid &isSolid)
{
  const glm::ivec3 cell(static_cast<int>(std::floor(bodyCenter.x + 0.5f)),
                        static_cast<int>(std::floor(bodyCenter.y + 0.5f)),
                        static_cast<int>(std::floor(bodyCenter.z + 0.5f)));
  if (isSolid(cell))
  {
    return cell + glm::ivec3(0, 1, 0);
  }
  return cell;
}

class UCreatureLightCache
{
public:
  struct State
  {
    glm::vec2 Current{1.0f, 0.0f};
    glm::vec2 Target{1.0f, 0.0f};
    glm::ivec3 Cell{0x7fffffff};
    float SecondsToResample{0.0f};
    uint64_t LastSeenFrame{0};
    /// False until the first light sample: that sample is taken as is (a
    /// mob spawned in a cave used to ease in from full daylight).
    bool Seeded{false};
  };

  /// State for `id` in creature generation `generation`, marked seen.
  State &Acquire(uint64_t id, uint64_t generation, uint64_t frame)
  {
    if (generation != Generation)
    {
      States.clear();
      Generation = generation;
    }
    State &state = States[id];
    state.LastSeenFrame = frame;
    return state;
  }

  /// New target sample (light of the cell the creature is in).
  static void SetTarget(State &state, const glm::vec2 &target)
  {
    state.Target = target;
    if (!state.Seeded)
    {
      state.Current = target;
      state.Seeded = true;
    }
  }

  /// Ease toward the target (~0.15 s) so crossing cell borders never pops.
  static const glm::vec2 &Ease(State &state, float dt)
  {
    const float blend = std::clamp(dt * 7.0f, 0.0f, 1.0f);
    state.Current += (state.Target - state.Current) * blend;
    return state.Current;
  }

  /// Drop creatures not drawn for more than `maxAgeFrames`.
  void Prune(uint64_t frame, uint64_t maxAgeFrames)
  {
    for (auto it = States.begin(); it != States.end();)
    {
      it = (frame - it->second.LastSeenFrame > maxAgeFrames) ? States.erase(it)
                                                             : std::next(it);
    }
  }

  size_t Size() const { return States.size(); }

private:
  uint64_t Generation{0};
  std::unordered_map<uint64_t, State> States;
};

} // namespace cutum

#endif
