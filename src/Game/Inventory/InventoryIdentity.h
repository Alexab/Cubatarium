#pragma once
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
namespace cutum
{
inline uint64_t InventoryInstanceIdentity(uint64_t existing,
                                          uint64_t minimum = 1)
{
  static std::atomic<uint64_t> serial{
      uint64_t(std::chrono::duration_cast<std::chrono::milliseconds>(
                   std::chrono::system_clock::now().time_since_epoch())
                   .count())
      << 16};
  const auto floor = std::max(minimum, existing ? existing + 1 : minimum);
  auto seen = serial.load(std::memory_order_relaxed);
  while (seen < floor &&
         !serial.compare_exchange_weak(seen, floor, std::memory_order_relaxed))
  {
  }
  return existing ? existing : serial.fetch_add(1, std::memory_order_relaxed);
}
} // namespace cutum
