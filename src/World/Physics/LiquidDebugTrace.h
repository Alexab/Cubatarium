#ifndef LIQUIDDEBUGTRACE_H
#define LIQUIDDEBUGTRACE_H

#include <glm/glm.hpp>
#include <array>
#include <cstddef>
#include <vector>

namespace cutum
{

struct LiquidDebugEntry
{
  glm::ivec3 From{0};
  glm::ivec3 To{0};
  char Reason[32]{};
};

class ULiquidDebugTrace
{
public:
  static constexpr size_t Capacity = 64;

  void Record(glm::ivec3 from, glm::ivec3 to, const char *reason);
  void CopyRecent(std::vector<LiquidDebugEntry> &out) const;
  void Clear();

  static ULiquidDebugTrace &Instance();

private:
  std::array<LiquidDebugEntry, Capacity> Entries{};
  size_t WriteIndex{0};
  size_t Count{0};
};

} // namespace cutum

#endif // LIQUIDDEBUGTRACE_H
