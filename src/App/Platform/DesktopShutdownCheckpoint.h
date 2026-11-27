#pragma once

#include "App/Platform/WindowsMemoryCheckpoint.h"
#include "Render/Engine/RenderImageStorageReport.h"
#include "Render/Engine/TerrainGpuBufferReport.h"

namespace cutum
{
inline constexpr std::array DesktopShutdownOwnerNames{
    "application", "core", "geometry", "world", "text_renderer", "view"};

struct DesktopShutdownCheckpoint
{
  WindowsTlsVectorHistory tlsHistory;
  WindowsMemoryCheckpoint beforeWindow, afterWindow, beforeOwners, afterOwners, afterContext;
  TerrainGpuBufferSnapshot buffersBefore, buffersAfter;
  RenderImageSnapshot imagesBefore, imagesAfter;
  std::array<bool, DesktopShutdownOwnerNames.size()> ownersBefore{}, ownersRetired{};
  bool contextBefore{}, contextAfterOwners{}, contextReleased{};

  bool Retired() const
  {
    const auto all = [](const auto &flags) { return std::all_of(flags.begin(), flags.end(), [](bool flag) { return flag; }); };
    return contextBefore && contextAfterOwners && contextReleased && all(ownersBefore) && all(ownersRetired) &&
        buffersBefore.contextAvailable && buffersAfter.contextAvailable &&
        buffersBefore.total.objects > 0 && !buffersAfter.total.objects &&
        !buffersBefore.failedQueries && !buffersAfter.failedQueries && !buffersBefore.overflow && !buffersAfter.overflow &&
        buffersAfter.namesWithoutObject == buffersBefore.namesWithoutObject + buffersBefore.total.objects &&
        imagesBefore.contextAvailable && imagesAfter.contextAvailable &&
        !imagesBefore.images.empty() && imagesAfter.images.empty() &&
        !imagesBefore.failedQueries && !imagesAfter.failedQueries && !imagesBefore.overflow && !imagesAfter.overflow &&
        !imagesAfter.imagesWithoutStorage && imagesAfter.namesWithoutObject == imagesBefore.namesWithoutObject +
            imagesBefore.imagesWithoutStorage + imagesBefore.images.size();
  }
};

inline nlohmann::json DescribeDesktopShutdown(const DesktopShutdownCheckpoint &row)
{
  const auto memory = [](const WindowsMemoryCheckpoint &checkpoint) {
    return nlohmann::json{{"checkpoint", DescribeWindowsMemoryCheckpoint(checkpoint)},
                          {"windows_gpu_memory", DescribeWindowsGpuMemory(checkpoint.driver)}};
  };
  const auto buffers = [](const TerrainGpuBufferSnapshot &snapshot) {
    return DescribeGpuBufferRoles(snapshot, "shutdown-selected-gpu-buffers.v1", 0, snapshot.roles.size());
  };
  auto owners = nlohmann::json::object();
  for (std::size_t i = 0; i < DesktopShutdownOwnerNames.size(); ++i)
    owners[DesktopShutdownOwnerNames[i]] = {{"present_before", row.ownersBefore[i]}, {"retired_before_context", row.ownersRetired[i]}};
  return {{"schema", "desktop-normal-shutdown.v1"}, {"status", row.Retired() ? "PASS" : "PARTIAL"},
      {"owners", std::move(owners)}, {"context_before", row.contextBefore},
      {"context_after_owners", row.contextAfterOwners}, {"context_released", row.contextReleased},
      {"before_window_initialization", memory(row.beforeWindow)},
      {"after_window_initialization", memory(row.afterWindow)},
      {"before_owner_release", memory(row.beforeOwners)}, {"after_owner_release", memory(row.afterOwners)},
      {"after_context_destroy", memory(row.afterContext)}, {"buffers_before", buffers(row.buffersBefore)},
      {"buffers_after", buffers(row.buffersAfter)}, {"images_before", DescribeRenderImages(row.imagesBefore)},
      {"images_after", DescribeRenderImages(row.imagesAfter)}, {"whole_runtime_complete", false}};
}
} // namespace cutum
