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
            {"live_bytes", s.LiveBytes()}, {"admitted_bytes", s.AdmittedBytes()},
            {"active_slots", s.activeSlots}, {"slot_receipts", s.slotReceipts},
            {"queued_receipts", s.queuedReceipts}, {"encoder_receipt", s.encoderReceipt},
            {"slot_path_bytes", s.slotPathBytes}, {"queued_path_bytes", s.queuedPathBytes},
            {"encoder_path_bytes", s.encoderPathBytes}, {"write_temporary_bytes", s.writeTemporaryBytes},
            {"metadata_bytes", s.MetadataBytes()}, {"tracked_live_bytes", s.TrackedLiveBytes()},
            {"live_output_files", s.liveOutputFiles}};
  };
  const auto write = [&](FlightCaptureOutput output) -> nlohmann::json {
    const auto &row = storage.lastWrite[static_cast<std::size_t>(output)];
    return {{"file_handle_observed", row.fileHandleObserved}, {"payload_bytes", row.payloadBytes},
            {"worker_thread_id", row.workerThreadId}, {"process_handles", row.processHandles},
            {"process_handles_observed", row.processHandlesObserved}, {"capture", describe(row.capture)},
            {"worker_stack", {{"observed", row.workerStack.observed}, {"reserved_bytes", row.workerStack.reserved},
                              {"committed_bytes", row.workerStack.committed}, {"guard_bytes", row.workerStack.guard}}},
            {"process_memory", {{"observed", row.process.observed}, {"working_set_bytes", row.process.workingSet},
                                {"peak_working_set_bytes", row.process.peakWorkingSet}, {"private_commit_bytes", row.process.privateCommit},
                                {"peak_private_commit_bytes", row.process.peakPrivateCommit}, {"paged_pool_quota_bytes", row.process.pagedPoolQuota},
                                {"nonpaged_pool_quota_bytes", row.process.nonpagedPoolQuota}}}};
  };
  return {{"schema", "flight-capture-cpu-storage.v1"}, {"codec_sampled", storage.codecSampled},
          {"service_inline_bytes", storage.serviceInlineBytes}, {"slot_limit", storage.slotLimit},
          {"queue_limit", storage.queueLimit}, {"accepted_frames", storage.acceptedFrames},
          {"persisted_frames", storage.persistedFrames}, {"peak_codec_live_request_bytes", storage.peakCodecLiveRequestBytes},
          {"current", describe(storage.current)}, {"peak_live", describe(storage.atPeakLive)},
          {"peak_admission", describe(storage.atPeakAdmission)}, {"peak_tracked", describe(storage.atPeakTracked)},
          {"path_backing_supported", storage.pathBackingSupported}, {"worker_handle_observed", storage.workerHandleObserved},
          {"worker_frame_inline_bytes", storage.workerFrameInlineBytes}, {"receipt_inline_bytes", storage.receiptInlineBytes},
          {"file_opens", storage.opens}, {"file_closes", storage.closes}, {"file_open_failures", storage.openFailures},
          {"file_close_failures", storage.closeFailures}, {"peak_open_files", storage.peakOpenFiles},
          {"png_write", write(FlightCaptureOutput::Png)}, {"receipt_write", write(FlightCaptureOutput::Receipt)}};
}
} // namespace cutum
