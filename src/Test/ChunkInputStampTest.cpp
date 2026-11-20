#include "World/Chunks/ChunkInputStamp.h"
#include <iostream>
int main()
{
  using namespace cutum;
  int failures = 0;
  auto check = [&](bool value, const char *message)
  {
    if (!value)
    {
      std::cerr << "FAIL: " << message << '\n';
      ++failures;
    }
  };
  UChunk chunk({0, 0, 0});
  auto stamp = ChunkInputStamp::Capture({0, 0, 0}, &chunk);
  check(stamp.Matches(&chunk), "origin is a valid dependency");
  chunk.SetBlockLocal({1, 1, 1}, static_cast<BlockId>(1));
  check(!stamp.Matches(&chunk), "content mutation invalidates");
  stamp = ChunkInputStamp::Capture({0, 0, 0}, &chunk);
  chunk.SetLightLocal({1, 1, 1}, 8, 3);
  check(!stamp.Matches(&chunk), "light mutation invalidates");
  stamp = ChunkInputStamp::Capture({0, 0, 0}, &chunk);
  chunk.SetLightLocal({1, 1, 1}, 8, 3);
  check(stamp.Matches(&chunk),
        "unchanged light does not create revision churn");
  chunk.ResetForReuse({0, 0, 0});
  check(!stamp.Matches(&chunk), "recycled coordinate is a new incarnation");
  check(!ChunkInputStamp::Capture({0, 0, 0}, nullptr).Matches(&chunk),
        "absence is not air");
  check(!ChunkInputStamp::Capture({0, 0, 0}, &chunk).Matches(nullptr),
        "unload invalidates");
  UChunk borders({-2, -4, 3});
  const auto empty = borders.GetBoundaryContentRevisions();
  borders.SetBlockLocal({7, 7, 7}, static_cast<BlockId>(1));
  check(borders.GetBoundaryContentRevisions() == empty,
        "interior content does not mutate any boundary plane clock");
  for (int axis = 0; axis < 3; ++axis)
    for (int side = 0; side < 2; ++side)
    {
      auto local = glm::ivec3(7);
      local[axis] = side ? CHUNK_SIZE - 1 : 0;
      auto expected = borders.GetBoundaryContentRevisions();
      ++expected[axis * 2 + side];
      borders.SetBlockLocal(local, static_cast<BlockId>(1));
      check(borders.GetBoundaryContentRevisions() == expected,
            "a boundary edit advances exactly its own plane");
      borders.SetBlockLocal(local, static_cast<BlockId>(1));
      check(borders.GetBoundaryContentRevisions() == expected,
            "no-op boundary writes do not churn proof clocks");
    }
  UChunk copied({0, 0, 0});
  copied = borders;
  check(copied.GetBoundaryContentRevisions() ==
            borders.GetBoundaryContentRevisions(),
        "detached chunk assignment preserves captured boundary clocks");
  const auto before_reuse = copied.GetIncarnation();
  copied.ResetForReuse({1, 2, 3});
  check(copied.GetBoundaryContentRevisions() == empty &&
            copied.GetIncarnation() != before_reuse,
        "reuse resets boundary clocks under a fresh incarnation");
  return failures ? 1 : 0;
}
