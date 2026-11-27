#pragma once

#include <cstddef>

namespace cutum
{
struct RgbaPngView
{
  const unsigned char *pixels{};
  std::size_t bytes{};
  int width{}, height{};
  bool bottomUp{};
};

struct PngAllocationObserver
{
  void *context{};
  void (*sample)(void *, std::size_t liveRequestBytes, std::size_t pendingRequestBytes) noexcept{};
};

using PngConsumer = bool (*)(void *, const unsigned char *, std::size_t, std::size_t);

std::size_t RgbaPngInputBytes(int width, int height) noexcept;
// The encoded bytes remain owned by this call through the consumer's return.
bool EncodeRgbaPng(RgbaPngView input, void *context, PngConsumer consume,
                   PngAllocationObserver observer = {});
} // namespace cutum
