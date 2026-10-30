#ifndef CREATURE_FACING_H
#define CREATURE_FACING_H

#include <glm/glm.hpp>
#include <optional>

namespace cutum
{

/// Direction (XZ) a creature should face after a move, or nothing to keep its
/// current yaw. The body turns toward where it actually went, but only when
/// that agrees with where it wanted to go: a push from a neighbour, a
/// depenetration or a knock-back (against or across the wish, or with no
/// wish at all) used to turn the body toward the push, which with oriented
/// boxes fed a per-frame turn/push loop. Wall slides still face the slide.
inline std::optional<glm::vec2> FacingDirectionFromMove(const glm::vec2 &moved,
                                                        const glm::vec2 &wish)
{
  constexpr float kMinMove = 1e-4f;
  constexpr float kMinWish = 1e-3f;
  // cos(72 deg): a slide along a wall hit at up to ~70 degrees still counts.
  constexpr float kMinAgreement = 0.3f;
  const float movedLen = glm::length(moved);
  const float wishLen = glm::length(wish);
  if (movedLen < kMinMove || wishLen < kMinWish)
  {
    return std::nullopt;
  }
  if (glm::dot(moved / movedLen, wish / wishLen) < kMinAgreement)
  {
    return std::nullopt;
  }
  return moved;
}

} // namespace cutum

#endif
