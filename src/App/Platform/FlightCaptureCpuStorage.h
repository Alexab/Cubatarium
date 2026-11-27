#pragma once

#include "Core/Memory/NativeThreadStack.h"
#include "Core/Memory/WindowsProcessMemory.h"
#include <array>
#include <cstddef>
#include <cstdint>

namespace cutum
{
struct FlightCaptureCpuSample
{
  std::size_t copiedSlots{}, queuedFrames{};
  std::uint64_t slotPixelBytes{}, queuedPixelBytes{}, encoderPixelBytes{};
  std::uint64_t codecLiveRequestBytes{}, codecPendingRequestBytes{};
  std::size_t activeSlots{}, slotReceipts{}, queuedReceipts{}, encoderReceipt{};
  std::uint64_t slotPathBytes{}, queuedPathBytes{}, encoderPathBytes{}, writeTemporaryBytes{};
  std::size_t liveOutputFiles{};

  std::uint64_t FrameBytes() const { return slotPixelBytes + queuedPixelBytes + encoderPixelBytes; }
  std::uint64_t LiveBytes() const { return FrameBytes() + codecLiveRequestBytes; }
  std::uint64_t AdmittedBytes() const { return LiveBytes() + codecPendingRequestBytes; }
  std::uint64_t MetadataBytes() const { return slotPathBytes + queuedPathBytes + encoderPathBytes + writeTemporaryBytes; }
  std::uint64_t TrackedLiveBytes() const { return LiveBytes() + MetadataBytes(); }
};

enum class FlightCaptureOutput { Png, Receipt, Count };

struct FlightCaptureWriteSample
{
  FlightCaptureCpuSample capture;
  WindowsProcessMemory process;
  NativeStackStorage workerStack;
  std::uint64_t payloadBytes{};
  std::uint32_t processHandles{}, workerThreadId{};
  bool processHandlesObserved{}, fileHandleObserved{};
};

struct FlightCaptureCpuStorage
{
  FlightCaptureCpuSample current, atPeakLive, atPeakAdmission;
  FlightCaptureCpuSample atPeakTracked;
  std::array<FlightCaptureWriteSample, static_cast<std::size_t>(FlightCaptureOutput::Count)> lastWrite{};
  std::uint64_t peakCodecLiveRequestBytes{}, acceptedFrames{}, persistedFrames{};
  std::uint64_t opens{}, closes{}, openFailures{}, closeFailures{};
  std::size_t serviceInlineBytes{}, slotLimit{}, queueLimit{};
  std::size_t workerFrameInlineBytes{}, receiptInlineBytes{}, peakOpenFiles{};
  bool codecSampled{};
  bool pathBackingSupported{}, workerHandleObserved{};
};
} // namespace cutum
