#pragma once

#include "App/Platform/FlightCaptureGpuCheckpoint.h"
#include "App/Platform/WindowManager.h"
#include "Core/Memory/ScopedPngEncoder.h"
#include "Test/GeometryGpuStorageFixture.h"
#include "ThirdParty/stb_image.h"

namespace cutum
{
struct EffectsGpuStorageProbe
{
  static bool Sky(UGeometryEngine &engine) { return engine.SkyGradientPass_.EnsureBuffers(); }
  static bool Cracks(UGeometryEngine &engine) { return engine.BlockCrackPass.EnsureCube(); }
  static bool Debris(UGeometryEngine &engine) { return engine.BlockBreakFx.EnsureGpu(); }
  static bool Influence(UGeometryEngine &engine) { return engine.InfluenceFx.EnsureLineBuffers(); }
  static bool Weather(UGeometryEngine &engine) { return engine.WeatherPass.EnsureParticleBuffers(); }

  static void SpawnDebris(UGeometryEngine &engine)
  {
    engine.BlockBreakFx.Particles.SpawnBreakBurst(glm::vec3(0.f), 80);
    engine.BlockBreakFx.Particles.Update(.01f);
  }

  static void DrawDebris(UGeometryEngine &engine)
  {
    engine.BlockBreakFx.DrawInstances(glm::mat4(1.f), {1, 0, 0}, {0, 1, 0});
  }
};

struct WindowCaptureGpuStorageProbe
{
  static void Attach(UWindowManager &manager) { manager.Window = glfwGetCurrentContext(); }
  static bool Queue(UWindowManager &manager, const std::filesystem::path &path, uint32_t index,
                    nlohmann::json receipt = nullptr)
  {
    return manager.QueueFlightCapture(path, index, 0.f, 0.f, GL_BACK, std::move(receipt));
  }
  static void Profile(UWindowManager &manager, PngAllocationObserver observer)
  {
    manager.SetFlightCaptureMemoryProbe(observer);
  }
  static FlightCaptureCpuStorage Cpu(const UWindowManager &manager)
  {
    return manager.InspectFlightCaptureCpuStorage();
  }
  static void Drain(UWindowManager &manager, FlightCaptureGpuCheckpoint &checkpoint)
  {
    manager.StopFlightCapture(&checkpoint);
  }
};
} // namespace cutum

namespace cutum::effects_gpu_storage_fixture
{
using geometry_gpu_storage_fixture::RejectUpload;
using gui_context_fixture::Require;
using shared_content_preview_fixture::Binding;
using Role = TerrainGpuBufferRole;

inline nlohmann::json Run(const std::filesystem::path &reportPath)
{
  creature_item_icon_fixture::PlatformFixture platform;
  const UGlStateScope state(kGlMaskFpViewmodel3D | GlStateBit::ColorMask);
  const GLint originalArray = Binding(GL_ARRAY_BUFFER_BINDING);
  const GLint originalPack = Binding(GL_PIXEL_PACK_BUFFER_BINDING);
  const std::array<GLenum, 4> packFields{GL_PACK_ALIGNMENT, GL_PACK_ROW_LENGTH, GL_PACK_SKIP_ROWS, GL_PACK_SKIP_PIXELS};
  std::array<GLint, 4> originalPackLayout{};
  for (size_t i = 0; i < packFields.size(); ++i)
    originalPackLayout[i] = Binding(packFields[i]);
  std::array<GLfloat, 4> originalClear{};
  glGetFloatv(GL_COLOR_CLEAR_VALUE, originalClear.data());

  GLuint sentinelVao{}, sentinelBuffer{}, query{};
  glGenVertexArrays(1, &sentinelVao);
  glGenBuffers(1, &sentinelBuffer);
  glGenQueries(1, &query);
  const auto bindSentinel = [&] {
    glBindVertexArray(sentinelVao);
    glBindBuffer(GL_ARRAY_BUFFER, sentinelBuffer);
  };
  bindSentinel();
  glBufferData(GL_ARRAY_BUFFER, 32, nullptr, GL_STATIC_DRAW);
  const auto bindings = [&] {
    return Binding(GL_VERTEX_ARRAY_BINDING) == GLint(sentinelVao) && Binding(GL_ARRAY_BUFFER_BINDING) == GLint(sentinelBuffer);
  };

  auto engine = std::make_unique<UGeometryEngine>(nullptr, nullptr, nullptr);
  const auto effectNames = [&] {
    TerrainGpuBufferCensus out;
    engine->InspectEffectsGpuBuffers(out);
    return out;
  };
  const auto extent = [](const TerrainGpuBufferSnapshot &s, Role role) {
    return s.roles[size_t(role)].bytes;
  };
  using Effects = EffectsGpuStorageProbe;
  const std::array initials{
      std::pair{&Effects::Sky, std::pair{GL_ARRAY_BUFFER, GL_STATIC_DRAW}},
      std::pair{&Effects::Cracks, std::pair{GL_ELEMENT_ARRAY_BUFFER, GL_STATIC_DRAW}},
      std::pair{&Effects::Debris, std::pair{GL_ELEMENT_ARRAY_BUFFER, GL_STATIC_DRAW}},
      std::pair{&Effects::Influence, std::pair{GL_ARRAY_BUFFER, GL_DYNAMIC_DRAW}},
      std::pair{&Effects::Weather, std::pair{GL_ELEMENT_ARRAY_BUFFER, GL_STATIC_DRAW}}};
  for (const auto &[initialize, upload] : initials)
  {
    RejectUpload refused(upload.first, upload.second);
    Require(!initialize(*engine) && refused.observed[0] && !glIsVertexArray(refused.observed[0]) &&
                refused.observed[1] && !glIsBuffer(refused.observed[1]) &&
                !glIsBuffer(refused.observed[2]) && !effectNames().Read().references && bindings(),
            "rejected effects upload retires partial buffers and preserves the caller binding");
  }
  for (const auto &initial : initials)
    Require(initial.first(*engine) && bindings(), "retry the real effects initializer");
  const auto initial = effectNames().Read();
  Require(initial.total.objects == 10 && extent(initial, Role::SkyGradient) == 80 && extent(initial, Role::BlockCracks) == 624 &&
              extent(initial, Role::BlockDebris) == 2104 && extent(initial, Role::InfluenceLines) == 1536 &&
              extent(initial, Role::WeatherParticles) == 7224, "measure all five production effect buffer layouts");
  Require(engine->InitEngine(), "initialize actual effect shaders for the draw controls");
  bindSentinel();
  PreviewRenderTarget drawTarget;
  Require(drawTarget.Initialize(64), "build the effect draw target");
  glBindFramebuffer(GL_FRAMEBUFFER, drawTarget.Framebuffer);
  glViewport(0, 0, 64, 64);
  Effects::SpawnDebris(*engine);
  const auto measureDraw = [&] {
    glBeginQuery(GL_PRIMITIVES_GENERATED, query);
    Effects::DrawDebris(*engine);
    glEndQuery(GL_PRIMITIVES_GENERATED);
    GLuint count{};
    glGetQueryObjectuiv(query, GL_QUERY_RESULT, &count);
    return count;
  };
  {
    RejectUpload refused(GL_ARRAY_BUFFER, GL_DYNAMIC_DRAW);
    Require(measureDraw() == 0 && refused.observed[0] && extent(effectNames().Read(), Role::BlockDebris) == 2104 && bindings(),
            "failed debris growth preserves the previous store and produces zero primitives");
  }
  Require(measureDraw() == 160 && bindings(), "same debris input retries and draws 80 quads");
  const auto names = effectNames();
  const auto grown = names.Read();
  Require(extent(grown, Role::BlockDebris) == 4408, "measure successful debris capacity growth");
  engine.reset();
  const auto retired = names.Read();
  Require(retired.namesWithoutObject == 10 && !retired.total.objects && bindings(), "engine teardown retires every effect buffer");

  UWindowManager capture;
  WindowCaptureGpuStorageProbe::Attach(capture);
  const auto output = std::filesystem::absolute(reportPath.string() + ".window-capture");
  Require(std::filesystem::create_directory(output), "create private window capture fixture directory");
  int width{}, height{};
  glfwGetFramebufferSize(glfwGetCurrentContext(), &width, &height);
  Require(width > 0 && height > 0, "fixture owns a live default framebuffer");
  const auto framebufferBytes = uint64_t(width) * uint64_t(height) * 4;
  const std::array<GLint, 4> hostileLayout{8, width + 17, 2, 5};
  for (size_t i = 0; i < packFields.size(); ++i)
    glPixelStorei(packFields[i], hostileLayout[i]);
  glBindBuffer(GL_PIXEL_PACK_BUFFER, sentinelBuffer);
  glBindFramebuffer(GL_READ_FRAMEBUFFER, drawTarget.Framebuffer);
  glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
  glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
  const auto packPreserved = [&] {
    for (size_t i = 0; i < packFields.size(); ++i)
      if (Binding(packFields[i]) != hostileLayout[i])
        return false;
    return Binding(GL_READ_FRAMEBUFFER_BINDING) == GLint(drawTarget.Framebuffer) &&
           Binding(GL_READ_BUFFER) == GL_COLOR_ATTACHMENT0 && Binding(GL_PIXEL_PACK_BUFFER_BINDING) == GLint(sentinelBuffer);
  };
  const auto queue = [&](uint32_t index) {
    return WindowCaptureGpuStorageProbe::Queue(capture, output / (std::to_string(index) + ".png"), index);
  };
  const auto captureNames = [&] {
    TerrainGpuBufferCensus out;
    capture.InspectFlightCaptureGpuBuffers(out);
    return out;
  };
  {
    RejectUpload refused(GL_PIXEL_PACK_BUFFER, GL_STREAM_READ);
    Require(!queue(0) && refused.packBuffer && !glIsBuffer(refused.packBuffer) &&
                !captureNames().Read().references && packPreserved(), "rejected pack allocation retires its buffer and restores layout");
  }
  const std::array<std::array<float, 3>, 4> colors{{{{1, 0, 0}}, {{0, 1, 0}}, {{0, 0, 1}}, {{1, 1, 0}}}};
  const auto paint = [&](size_t slot) {
    const auto &color = colors[slot];
    glClearColor(color[0], color[1], color[2], 1.f);
    glClear(GL_COLOR_BUFFER_BIT);
  };
  paint(0);
  Require(queue(1) && packPreserved(), "retry tightly packed capture with the same hostile layout");
  glFinish();
  Require(capture.IsDiagnosticCaptureReady() && packPreserved(), "copy the completed first readback and return its slot");
  {
    RejectUpload wouldRefuse(GL_PIXEL_PACK_BUFFER, GL_STREAM_READ);
    paint(1);
    Require(queue(2) && !wouldRefuse.packBuffer && packPreserved(), "same framebuffer extent reuses the idle pack store");
  }
  paint(2);
  Require(queue(3) && packPreserved(), "admit second pending readback");
  paint(3);
  Require(queue(4) && !queue(5) && packPreserved(), "three slots reject a fourth pending readback without replacing it");
  FlightCaptureGpuCheckpoint captureCheckpoint;
  WindowCaptureGpuStorageProbe::Drain(capture, captureCheckpoint);
  Require(captureCheckpoint.beforeShutdown.total.objects == 3 &&
              extent(captureCheckpoint.beforeShutdown, Role::WindowCapture) == framebufferBytes * 3 &&
              captureCheckpoint.afterShutdown.namesWithoutObject == 3 && !captureCheckpoint.afterShutdown.total.objects &&
              !captureNames().Read().references && packPreserved(), "shutdown drains accepted frames and retires all three pack stores");
  for (size_t i = 0; i < colors.size(); ++i)
  {
    int w{}, h{}, channels{};
    const auto path = output / (std::to_string(i + 1) + ".png");
    const std::unique_ptr<stbi_uc, decltype(&stbi_image_free)> pixels(stbi_load(path.string().c_str(), &w, &h, &channels, 4), stbi_image_free);
    Require(pixels && w == width && h == height, "read persisted PNG from each admitted capture");
    const auto center = 4 * (size_t(height / 2) * size_t(width) + size_t(width / 2));
    for (size_t channel = 0; channel < 3; ++channel)
      Require(pixels.get()[center + channel] == colors[i][channel] * 255, "persisted PNG uses the pixels from the accepted frame");
  }
  Require(!std::filesystem::exists(output / "0.png") && !std::filesystem::exists(output / "5.png") &&
              glGetError() == GL_NO_ERROR, "only admitted capture images were persisted");

  glClearColor(originalClear[0], originalClear[1], originalClear[2], originalClear[3]);
  for (size_t i = 0; i < packFields.size(); ++i)
    glPixelStorei(packFields[i], originalPackLayout[i]);
  glBindBuffer(GL_PIXEL_PACK_BUFFER, GLuint(originalPack));
  glBindBuffer(GL_ARRAY_BUFFER, GLuint(originalArray));
  glDeleteBuffers(1, &sentinelBuffer);
  glDeleteVertexArrays(1, &sentinelVao);
  glDeleteQueries(1, &query);
  return {{"status", "PASS"}, {"initial_effects", DescribeEffectsGpuBuffers(initial)}, {"grown_effects", DescribeEffectsGpuBuffers(grown)},
          {"refused_initializers", initials.size()}, {"retired_effect_names", retired.namesWithoutObject}, {"debris_primitives_after_retry", 160},
          {"capture_before_shutdown", DescribeEffectsGpuBuffers(captureCheckpoint.beforeShutdown)},
          {"capture_retired_names", captureCheckpoint.afterShutdown.namesWithoutObject}, {"capture_frame_bytes", framebufferBytes},
          {"persisted_capture_images", colors.size()}};
}
} // namespace cutum::effects_gpu_storage_fixture
