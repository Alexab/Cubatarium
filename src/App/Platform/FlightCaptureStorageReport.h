#pragma once

#include "App/Platform/FlightCaptureCpuStorage.h"
#include <nlohmann/json.hpp>

namespace cutum
{
inline nlohmann::json DescribeFlightCaptureCpuStorage(const FlightCaptureCpuStorage &storage)
{
  const auto describe = [](const FlightCaptureCpuSample &s) -> nlohmann::json {
    return {{"copied_slots", s.copiedSlots}, {"queued_frames", s.queuedFrames},
            {"slot_pixel_bytes", s.slotPixelBytes}, {"queued_pixel_bytes", s.queuedPixelBytes},
            {"encoder_pixel_bytes", s.encoderPixelBytes}, {"codec_live_request_bytes", s.codecLiveRequestBytes},
            {"codec_pending_request_bytes", s.codecPendingRequestBytes}, {"frame_pixel_bytes", s.FrameBytes()},
            {"live_bytes", s.LiveBytes()}, {"admitted_bytes", s.AdmittedBytes()}};
  };
  return {{"schema", "flight-capture-cpu-storage.v1"}, {"codec_sampled", storage.codecSampled},
          {"service_inline_bytes", storage.serviceInlineBytes}, {"slot_limit", storage.slotLimit},
          {"queue_limit", storage.queueLimit}, {"accepted_frames", storage.acceptedFrames},
          {"persisted_frames", storage.persistedFrames}, {"peak_codec_live_request_bytes", storage.peakCodecLiveRequestBytes},
          {"current", describe(storage.current)}, {"peak_live", describe(storage.atPeakLive)},
          {"peak_admission", describe(storage.atPeakAdmission)}};
}
} // namespace cutum
