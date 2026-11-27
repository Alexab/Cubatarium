#pragma once

#include "App/Platform/WindowsMemoryCheckpoint.h"
#include "Render/GlIncludes.h"
#include <GLFW/glfw3.h>
#include <chrono>
#include <fstream>
#include <stdexcept>
#include <string>
#include <thread>

namespace cutum::windows_gpu_memory_fixture
{
#if CUTUM_WINDOWS_GPU_MEMORY_API
inline void Require(bool condition, const char *message)
{
  if (!condition)
    throw std::runtime_error(message);
}

struct NodeAddress
{
  std::uint32_t luidHigh, luidLow, index;
  bool nonLocal;
};

inline std::optional<std::uint64_t> CurrentUsage(const WindowsGpuMemory &memory, NodeAddress address)
{
  for (const auto &adapter : memory.adapters)
    if (adapter.luidHigh == address.luidHigh && adapter.luidLow == address.luidLow)
      for (const auto &node : adapter.nodes)
        if (node.index == address.index)
        {
          const auto &segment = address.nonLocal ? node.nonLocal : node.local;
          if (segment.status == 0)
            return segment.usage;
        }
  return {};
}

inline int Run(GLFWwindow *window, const char *outPath, const WindowsMemoryCheckpoint &cold)
{
  constexpr GLsizei side = 4096;
  constexpr std::uint64_t textureBytes = std::uint64_t{side} * side * 4;
  constexpr std::uint64_t observableDelta = textureBytes / 4;
  const auto *renderer = reinterpret_cast<const char *>(glGetString(GL_RENDERER));
  Require(renderer != nullptr, "identify the driver of the current OpenGL context");
  const std::string rendererName(renderer);
  const auto warm = ReadWindowsMemoryCheckpoint();
  WindowsMemoryCheckpoint live;
  GLuint textureName{};
  {
    struct TextureTarget
    {
      GLuint texture{}, framebuffer{};
      ~TextureTarget()
      {
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glDeleteFramebuffers(1, &framebuffer);
        glDeleteTextures(1, &texture);
      }
    } target;
    std::vector<std::uint32_t> pixels(static_cast<std::size_t>(side) * side);
    for (std::size_t i = 0; i < pixels.size(); ++i)
      pixels[i] = (static_cast<std::uint32_t>(i) * 2654435761u) | 0xff000000u;
    glGenTextures(1, &target.texture);
    glBindTexture(GL_TEXTURE_2D, target.texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, side, side, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
    glGenFramebuffers(1, &target.framebuffer);
    glBindFramebuffer(GL_FRAMEBUFFER, target.framebuffer);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, target.texture, 0);
    Require(glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE,
            "attach the authored texture to the fixture framebuffer");
    std::uint32_t first{}, last{};
    glReadPixels(0, 0, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, &first);
    glReadPixels(side - 1, side - 1, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, &last);
    glFinish();
    Require(first == pixels.front() && last == pixels.back() && glGetError() == GL_NO_ERROR,
            "verify the authored texture is live in the actual OpenGL context");
    textureName = target.texture;
    live = ReadWindowsMemoryCheckpoint();
  }
  glFinish();
  Require(!glIsTexture(textureName) && glGetError() == GL_NO_ERROR,
          "retire the OpenGL object before measuring the separate driver charge");
  const auto deleted = ReadWindowsMemoryCheckpoint();
  glfwMakeContextCurrent(nullptr);
  glfwDestroyWindow(window);
  glfwTerminate();

  Require(cold.driver.complete && warm.driver.complete && live.driver.complete && deleted.driver.complete,
          "observe every enumerated physical node and close all adapter query handles at each stage");
  std::optional<NodeAddress> growing;
  std::uint64_t warmUsage{}, liveUsage{};
  for (const auto &adapter : live.driver.adapters)
    if (adapter.renderSupported && !adapter.softwareDevice)
      for (const auto &node : adapter.nodes)
        for (const auto nonLocal : {false, true})
        {
          const NodeAddress address{adapter.luidHigh, adapter.luidLow, node.index, nonLocal};
          const auto &segment = nonLocal ? node.nonLocal : node.local;
          const auto baseline = CurrentUsage(warm.driver, address);
          if (baseline && segment.usage >= *baseline && segment.usage - *baseline >= observableDelta &&
              segment.usage - *baseline > liveUsage - warmUsage)
          {
            growing = address;
            warmUsage = *baseline;
            liveUsage = segment.usage;
          }
        }
  Require(growing.has_value(), "the real upload increases current process usage on a hardware adapter segment");
  WindowsMemoryCheckpoint retired;
  std::optional<std::uint64_t> retiredUsage;
  for (int sample = 0; sample < 12; ++sample)
  {
    retired = ReadWindowsMemoryCheckpoint();
    retiredUsage = CurrentUsage(retired.driver, *growing);
    if (retired.driver.complete && retiredUsage && *retiredUsage <= liveUsage - observableDelta)
      break;
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  }
  Require(retired.driver.complete && retiredUsage && *retiredUsage <= liveUsage - observableDelta,
          "observe an actual process usage decrease after graphics context retirement");
  const auto deletedUsage = CurrentUsage(deleted.driver, *growing);
  Require(deletedUsage.has_value(), "retain the adapter identity across texture retirement and context teardown");
  const nlohmann::json report{
      {"schema", "windows-gpu-memory-fixture.v1"}, {"status", "PASS"}, {"gl_renderer", rendererName},
      {"texture_bytes", textureBytes}, {"verified_pixels", 2}, {"texture_retired", true},
      {"observed_adapter", {{"luid_high", growing->luidHigh}, {"luid_low", growing->luidLow},
          {"physical_adapter_index", growing->index}, {"segment", growing->nonLocal ? "non_local" : "local"}}},
      {"warm_usage_bytes", warmUsage}, {"live_usage_bytes", liveUsage},
      {"texture_deleted_usage_bytes", *deletedUsage}, {"context_retired_usage_bytes", *retiredUsage},
      {"cold", DescribeWindowsGpuMemory(cold.driver)}, {"warm", DescribeWindowsGpuMemory(warm.driver)},
      {"live", DescribeWindowsGpuMemory(live.driver)}, {"texture_deleted", DescribeWindowsGpuMemory(deleted.driver)},
      {"context_retired", DescribeWindowsGpuMemory(retired.driver)},
      {"process_checkpoints", {{"cold", DescribeWindowsMemoryCheckpoint(cold)},
          {"warm", DescribeWindowsMemoryCheckpoint(warm)}, {"live", DescribeWindowsMemoryCheckpoint(live)},
          {"texture_deleted", DescribeWindowsMemoryCheckpoint(deleted)},
          {"context_retired", DescribeWindowsMemoryCheckpoint(retired)}}}};
  std::ofstream output(outPath);
  output << report.dump(2) << '\n';
  Require(static_cast<bool>(output), "write the GPU memory lifecycle report");
  return 0;
}
#endif
} // namespace cutum::windows_gpu_memory_fixture
