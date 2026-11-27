#pragma once

#include <cstddef>
#include <cstdint>

namespace cutum
{
struct FlightCaptureCpuSample
{
  std::size_t copiedSlots{}, queuedFrames{};
  std::uint64_t slotPixelBytes{}, queuedPixelBytes{}, encoderPixelBytes{};
  std::uint64_t codecLiveRequestBytes{}, codecPendingRequestBytes{};

  std::uint64_t FrameBytes() const { return slotPixelBytes + queuedPixelBytes + encoderPixelBytes; }
  std::uint64_t LiveBytes() const { return FrameBytes() + codecLiveRequestBytes; }
  std::uint64_t AdmittedBytes() const { return LiveBytes() + codecPendingRequestBytes; }
};

struct FlightCaptureCpuStorage
{
  FlightCaptureCpuSample current, atPeakLive, atPeakAdmission;
  std::uint64_t peakCodecLiveRequestBytes{}, acceptedFrames{}, persistedFrames{};
  std::size_t serviceInlineBytes{}, slotLimit{}, queueLimit{};
  bool codecSampled{};
};
} // namespace cutum
