#pragma once

#include "App/Platform/FlightCaptureStorageReport.h"
#include "Core/Memory/StbiAllocation.h"
#include "Test/EffectsGpuStorageFixture.h"
#include <condition_variable>
#include <fstream>
#include <iomanip>
#include <limits>
#include <mutex>
#include <sstream>

namespace cutum::flight_capture_cpu_storage_fixture
{
using gui_context_fixture::Require;
using Probe = WindowCaptureGpuStorageProbe;

class CodecGate
{
public:
  static void Observe(void *context, size_t live, size_t pending) noexcept
  {
    if (!live || pending)
      return;
    auto &gate = *static_cast<CodecGate *>(context);
    std::unique_lock lock(gate.Mutex);
    if (gate.Entered)
      return;
    gate.Entered = true;
    gate.Changed.notify_all();
    gate.Changed.wait(lock, [&] { return gate.Released; });
  }

  bool Wait()
  {
    std::unique_lock lock(Mutex);
    return Changed.wait_for(lock, std::chrono::seconds(10), [&] { return Entered; });
  }

  void Release()
  {
    std::lock_guard lock(Mutex);
    Released = true;
    Changed.notify_all();
  }

private:
  std::mutex Mutex;
  std::condition_variable Changed;
  bool Entered{}, Released{};
};

inline nlohmann::json Run(const std::filesystem::path &reportPath)
{
  const UGlStateScope state(kGlMaskFpViewmodel3D | GlStateBit::ColorMask);
  CodecGate gate;
  UWindowManager capture;
  struct ReleaseGate { CodecGate &gate; ~ReleaseGate() { gate.Release(); } } release{gate};
  Probe::Attach(capture);
  Probe::Profile(capture, {&gate, CodecGate::Observe});
  const auto output = std::filesystem::absolute(reportPath.string() + ".capture-cpu");
  Require(std::filesystem::create_directory(output), "create isolated capture CPU fixture directory");
  int width{}, height{};
  glfwGetFramebufferSize(glfwGetCurrentContext(), &width, &height);
  const auto bytes = RgbaPngInputBytes(width, height);
  Require(height > 1 && bytes == size_t(width) * size_t(height) * 4 &&
              RgbaPngInputBytes(1280, 720) == 3686400 && !RgbaPngInputBytes(0, 1) &&
              !RgbaPngInputBytes(std::numeric_limits<int>::max(), 2) &&
              !RgbaPngInputBytes(8192, std::numeric_limits<int>::max()),
          "check rectangular encoder extents and signed length refusal");

  std::array<GLfloat, 4> originalClear{};
  std::array<GLint, 4> originalScissor{};
  glGetFloatv(GL_COLOR_CLEAR_VALUE, originalClear.data());
  glGetIntegerv(GL_SCISSOR_BOX, originalScissor.data());
  const auto scissorEnabled = glIsEnabled(GL_SCISSOR_TEST);
  glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
  glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
  glDisable(GL_SCISSOR_TEST);
  glClearColor(1.f, 0.f, 0.f, 1.f);
  glClear(GL_COLOR_BUFFER_BIT);
  glEnable(GL_SCISSOR_TEST);
  glScissor(0, height / 2, width, height - height / 2);
  glClearColor(0.f, 1.f, 0.f, 1.f);
  glClear(GL_COLOR_BUFFER_BIT);
  glScissor(originalScissor[0], originalScissor[1], originalScissor[2], originalScissor[3]);
  if (!scissorEnabled)
    glDisable(GL_SCISSOR_TEST);
  glClearColor(originalClear[0], originalClear[1], originalClear[2], originalClear[3]);

  const auto queue = [&](uint32_t index, nlohmann::json receipt = nullptr) {
    return Probe::Queue(capture, output / (std::to_string(index) + ".png"), index, std::move(receipt));
  };
  Require(queue(0, {{"schema", "diagnostic-capture-receipt.v1"}, {"capture_id", 0}}), "start the encoder on a real frame");
  glFinish();
  Require(capture.IsDiagnosticCaptureReady() && gate.Wait(), "hold the encoder with its first live codec allocation");
  for (uint32_t i = 1; i <= 4; ++i)
  {
    Require(queue(i), "accept another frame for the fixed encoder queue");
    glFinish();
    Require(capture.IsDiagnosticCaptureReady(), "copy another frame to the available queue slot");
  }
  for (uint32_t i = 5; i <= 7; ++i)
    Require(queue(i), "fill every GPU readback slot behind the full queue");
  glFinish();
  Require(!capture.IsDiagnosticCaptureReady() && !queue(8), "keep completed slots under backpressure at the eight-frame limit");
  const auto saturated = Probe::Cpu(capture);
  const auto &live = saturated.current;
  Require(saturated.slotLimit == 3 && saturated.queueLimit == 4 && saturated.codecSampled && saturated.acceptedFrames == 8 &&
              saturated.persistedFrames == 0 && live.codecLiveRequestBytes > bytes && !live.codecPendingRequestBytes &&
              live.copiedSlots == 3 && live.queuedFrames == 4 && live.slotPixelBytes == 3 * bytes &&
              live.queuedPixelBytes == 4 * bytes && live.encoderPixelBytes == bytes && live.FrameBytes() == 8 * bytes,
          "observe eight simultaneous real frame backings and live PNG scratch");
  gate.Release();
  FlightCaptureGpuCheckpoint checkpoint;
  Probe::Drain(capture, checkpoint);
  const auto &drained = checkpoint.cpuAfterShutdown;
  Require(drained.current.AdmittedBytes() == 0 && drained.current.copiedSlots == 0 && drained.current.queuedFrames == 0 &&
              drained.acceptedFrames == 8 && drained.persistedFrames == 8 && drained.atPeakLive.LiveBytes() >= live.LiveBytes() &&
              drained.atPeakAdmission.AdmittedBytes() >= drained.atPeakLive.LiveBytes() &&
              drained.peakCodecLiveRequestBytes > 0 && checkpoint.afterShutdown.namesWithoutObject == 3,
          "drain every accepted frame and retire CPU backing, codec allocations and pack stores");

  for (uint32_t i = 0; i < 8; ++i)
  {
    int w{}, h{}, components{};
    const auto file = output / (std::to_string(i) + ".png");
    const std::unique_ptr<stbi_uc, decltype(&stbi_image_free)> pixels(stbi_load(file.string().c_str(), &w, &h, &components, 4), stbi_image_free);
    const auto bottomRow = size_t(height - 1) * width * 4;
    Require(pixels && w == width && h == height && pixels.get()[0] == 0 && pixels.get()[1] == 255 &&
                pixels.get()[bottomRow] == 255 && pixels.get()[bottomRow + 1] == 0,
            "PNG orientation preserves the asymmetric upper and lower frame colors");
  }
  Require(!std::filesystem::exists(output / "8.png"), "no persisted output for the unadmitted frame");
  std::ifstream png(output / "0.png", std::ios::binary);
  uint64_t checksum = 14695981039346656037ull;
  for (char byte{}; png.get(byte);)
  {
    checksum ^= static_cast<unsigned char>(byte);
    checksum *= 1099511628211ull;
  }
  Require(png.eof() && !png.bad(), "independently hash the persisted PNG bytes");
  std::ostringstream hex;
  hex << std::hex << std::setw(16) << std::setfill('0') << checksum;
  std::ifstream receiptFile(output / "0.png.json", std::ios::binary);
  nlohmann::json receipt;
  receiptFile >> receipt;
  Require(receipt.at("png_fnv1a64") == hex.str() && receipt.at("width") == width && receipt.at("height") == height &&
              receipt.at("capture_index") == 0, "capture receipt matches the actual persisted file");

  const std::array<unsigned char, 48> source{};
  const RgbaPngView image{source.data(), source.size(), 4, 3, true};
  const auto sink = [](void *context, const unsigned char *, size_t, size_t) {
    ++*static_cast<unsigned *>(context);
    return true;
  };
  const auto denied = std::make_shared<UStbiAllocationLedger>(std::numeric_limits<size_t>::max(), 3);
  unsigned published{};
  {
    const UStbiAllocationScope ledger(denied);
    Require(!EncodeRgbaPng(image, &published, sink) && !published, "refuse an actual PNG codec request before publication");
  }
  const auto failed = denied->GetSnapshot();
  Require(failed.refusals == 1 && failed.successful_allocations == 3 && !failed.live_request_bytes &&
              !failed.reserved_request_bytes && EncodeRgbaPng(image, &published, sink) && published == 1 &&
              glGetError() == GL_NO_ERROR, "retire partial codec backing and retry the same image");
  return {{"status", "PASS"}, {"frame_pixel_bytes", bytes}, {"saturated", DescribeFlightCaptureCpuStorage(saturated)},
          {"after_shutdown", DescribeFlightCaptureCpuStorage(drained)}, {"retired_gpu_buffers", checkpoint.afterShutdown.namesWithoutObject},
          {"verified_images", 8}, {"verified_receipt", true}, {"codec_refusal_retried", true}};
}
} // namespace cutum::flight_capture_cpu_storage_fixture
