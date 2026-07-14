#pragma once

#include "World/Chunks/ChunkManager.h"
#include <cstddef>
#include <vector>

namespace cutum
{

class UBlockWorld;

class UBlockCountTracker
{
public:
  void Reset(size_t value);
  void OnBlockChanged(bool was_air, bool is_air);
  size_t GetCount() const { return Count; }
  bool NeedsRecount() const { return NeedsRecountFlag; }
  void MarkNeedsRecount();
  void TickRecount(const UBlockWorld &world, int max_chunks);

private:
  size_t Count{0};
  bool NeedsRecountFlag{false};
  std::vector<glm::ivec3> RecountQueue;
  size_t RecountIndex{0};
};

} // namespace cutum
