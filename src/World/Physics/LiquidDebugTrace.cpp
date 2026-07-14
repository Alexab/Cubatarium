#include "World/Physics/LiquidDebugTrace.h"
#include <cstring>

namespace cutum
{

void ULiquidDebugTrace::Record(glm::ivec3 from, glm::ivec3 to, const char *reason)
{
  LiquidDebugEntry &entry = Entries[WriteIndex];
  entry.From = from;
  entry.To = to;
  if (reason)
  {
    std::strncpy(entry.Reason, reason, sizeof(entry.Reason) - 1);
    entry.Reason[sizeof(entry.Reason) - 1] = '\0';
  }
  else
  {
    entry.Reason[0] = '\0';
  }
  WriteIndex = (WriteIndex + 1) % Capacity;
  Count = std::min(Count + 1, Capacity);
}

void ULiquidDebugTrace::CopyRecent(std::vector<LiquidDebugEntry> &out) const
{
  out.clear();
  out.reserve(Count);
  const size_t start = Count < Capacity ? 0 : WriteIndex;
  for (size_t i = 0; i < Count; ++i)
  {
    out.push_back(Entries[(start + i) % Capacity]);
  }
}

void ULiquidDebugTrace::Clear()
{
  WriteIndex = 0;
  Count = 0;
}

ULiquidDebugTrace &ULiquidDebugTrace::Instance()
{
  static ULiquidDebugTrace trace;
  return trace;
}

} // namespace cutum
