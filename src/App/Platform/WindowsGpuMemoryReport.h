#pragma once

#include "Core/Memory/WindowsGpuMemory.h"
#include <nlohmann/json.hpp>
#include <utility>

namespace cutum
{
inline nlohmann::json DescribeWindowsGpuMemory(const WindowsGpuMemory &memory)
{
  using Json = nlohmann::json;
  const auto status = [](std::optional<std::int32_t> value) {
    return value ? Json(*value) : Json(nullptr);
  };
  const auto segment = [&](const WindowsGpuMemorySegment &value) {
    const bool observed = value.status == 0;
    return Json{{"ntstatus", value.status}, {"observed", observed},
        {"current_usage_bytes", observed ? Json(value.usage) : Json(nullptr)},
        {"budget_bytes", observed ? Json(value.budget) : Json(nullptr)},
        {"current_reservation_bytes", observed ? Json(value.reservation) : Json(nullptr)},
        {"available_for_reservation_bytes", observed ? Json(value.availableForReservation) : Json(nullptr)}};
  };
  auto adapters = Json::array();
  for (const auto &adapter : memory.adapters)
  {
    auto nodes = Json::array();
    for (const auto &node : adapter.nodes)
      nodes.push_back({{"physical_adapter_index", node.index}, {"local", segment(node.local)},
                       {"non_local", segment(node.nonLocal)}});
    const bool typeObserved = adapter.typeStatus == 0;
    adapters.push_back({{"luid_high", adapter.luidHigh}, {"luid_low", adapter.luidLow},
        {"video_present_sources", adapter.videoSources}, {"type_ntstatus", status(adapter.typeStatus)},
        {"render_supported", typeObserved ? Json(adapter.renderSupported) : Json(nullptr)},
        {"software_device", typeObserved ? Json(adapter.softwareDevice) : Json(nullptr)},
        {"physical_count_ntstatus", status(adapter.physicalCountStatus)},
        {"physical_adapter_count", adapter.physicalCountStatus == 0 ? Json(adapter.physicalAdapterCount) : Json(nullptr)},
        {"close_ntstatus", status(adapter.closeStatus)}, {"nodes", std::move(nodes)}});
  }
  return {{"schema", "windows-wddm-process-gpu-memory.v1"},
      {"status", memory.complete ? "PASS" : !memory.apiAvailable ? "UNSUPPORTED" :
                 memory.adapters.empty() ? "UNOBSERVED" : "PARTIAL"},
      {"capacity_ntstatus", status(memory.capacityStatus)}, {"enumeration_ntstatus", status(memory.enumerationStatus)},
      {"scope", "current_process_per_physical_adapter_and_segment"}, {"adapters", std::move(adapters)},
      {"whole_runtime_complete", false}};
}
} // namespace cutum
