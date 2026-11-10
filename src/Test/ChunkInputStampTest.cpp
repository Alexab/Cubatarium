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
  return failures ? 1 : 0;
}
