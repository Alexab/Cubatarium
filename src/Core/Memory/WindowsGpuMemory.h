#pragma once

#include <cstdint>
#include <optional>
#include <vector>

#if defined(_WIN32) && __has_include(<d3dkmthk.h>)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <d3dkmthk.h>
#define CUTUM_WINDOWS_GPU_MEMORY_API 1
#else
#define CUTUM_WINDOWS_GPU_MEMORY_API 0
#endif

namespace cutum
{
struct WindowsGpuMemorySegment
{
  std::uint64_t usage{}, budget{}, reservation{}, availableForReservation{};
  std::int32_t status{};
};

struct WindowsGpuMemoryNode
{
  std::uint32_t index{};
  WindowsGpuMemorySegment local, nonLocal;
};

struct WindowsGpuMemoryAdapter
{
  std::uint32_t luidHigh{}, luidLow{}, videoSources{}, physicalAdapterCount{};
  bool renderSupported{}, softwareDevice{};
  std::optional<std::int32_t> typeStatus, physicalCountStatus, closeStatus;
  std::vector<WindowsGpuMemoryNode> nodes;
};

struct WindowsGpuMemory
{
  std::optional<std::int32_t> capacityStatus, enumerationStatus;
  std::vector<WindowsGpuMemoryAdapter> adapters;
  bool apiAvailable{}, complete{};
};

#if CUTUM_WINDOWS_GPU_MEMORY_API
namespace windows_gpu_memory_detail
{
struct Gdi32
{
  HMODULE module = LoadLibraryExW(L"gdi32.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
  ~Gdi32() { if (module) FreeLibrary(module); }
};

struct OpenAdapters
{
  std::vector<D3DKMT_ADAPTERINFO> entries;
  decltype(&D3DKMTCloseAdapter) closeAdapter;

  std::optional<std::int32_t> Close(D3DKMT_ADAPTERINFO &entry) const noexcept
  {
    if (!entry.hAdapter)
      return {};
    const D3DKMT_CLOSEADAPTER request{entry.hAdapter};
    const auto status = closeAdapter(&request);
    if (status == 0)
      entry.hAdapter = 0;
    return status;
  }

  ~OpenAdapters()
  {
    for (auto &entry : entries)
      Close(entry);
  }
};
} // namespace windows_gpu_memory_detail
#endif

// Reports process-attributed WDDM usage after opening only adapter query handles.
// Each physical node and segment is sampled separately. These charges are
// separate observations from engine object payload and PSAPI process memory.
inline WindowsGpuMemory ReadWindowsGpuMemory()
{
  WindowsGpuMemory result;
#if CUTUM_WINDOWS_GPU_MEMORY_API
  using namespace windows_gpu_memory_detail;
  const Gdi32 gdi;
  if (!gdi.module)
    return result;
  const auto enumerate = reinterpret_cast<decltype(&D3DKMTEnumAdapters2)>(
      GetProcAddress(gdi.module, "D3DKMTEnumAdapters2"));
  const auto queryAdapter = reinterpret_cast<decltype(&D3DKMTQueryAdapterInfo)>(
      GetProcAddress(gdi.module, "D3DKMTQueryAdapterInfo"));
  const auto queryMemory = reinterpret_cast<decltype(&D3DKMTQueryVideoMemoryInfo)>(
      GetProcAddress(gdi.module, "D3DKMTQueryVideoMemoryInfo"));
  const auto closeAdapter = reinterpret_cast<decltype(&D3DKMTCloseAdapter)>(
      GetProcAddress(gdi.module, "D3DKMTCloseAdapter"));
  if (!enumerate || !queryAdapter || !queryMemory || !closeAdapter)
    return result;
  result.apiAvailable = true;

  D3DKMT_ENUMADAPTERS2 request{};
  result.capacityStatus = enumerate(&request);
  if (*result.capacityStatus || !request.NumAdapters)
    return result;
  OpenAdapters handles{std::vector<D3DKMT_ADAPTERINFO>(request.NumAdapters), closeAdapter};
  request.pAdapters = handles.entries.data();
  result.enumerationStatus = enumerate(&request);
  if (*result.enumerationStatus || !request.NumAdapters || request.NumAdapters > handles.entries.size())
    return result;
  result.complete = true;
  result.adapters.reserve(request.NumAdapters);
  const auto process = GetCurrentProcess();
  for (std::uint32_t i = 0; i < request.NumAdapters; ++i)
  {
    auto &entry = handles.entries[i];
    auto &adapter = result.adapters.emplace_back();
    adapter.luidHigh = static_cast<std::uint32_t>(entry.AdapterLuid.HighPart);
    adapter.luidLow = entry.AdapterLuid.LowPart;
    adapter.videoSources = entry.NumOfSources;
    if (!entry.hAdapter)
    {
      result.complete = false;
      continue;
    }
    D3DKMT_ADAPTERTYPE type{};
    D3DKMT_QUERYADAPTERINFO info{entry.hAdapter, KMTQAITYPE_ADAPTERTYPE, &type, sizeof(type)};
    adapter.typeStatus = queryAdapter(&info);
    if (*adapter.typeStatus == 0)
    {
      adapter.renderSupported = type.RenderSupported != 0;
      adapter.softwareDevice = type.SoftwareDevice != 0;
    }
    D3DKMT_PHYSICAL_ADAPTER_COUNT count{};
    info.Type = KMTQAITYPE_PHYSICALADAPTERCOUNT;
    info.pPrivateDriverData = &count;
    info.PrivateDriverDataSize = sizeof(count);
    adapter.physicalCountStatus = queryAdapter(&info);
    if (*adapter.physicalCountStatus == 0)
      adapter.physicalAdapterCount = count.Count;
    adapter.nodes.reserve(adapter.physicalAdapterCount);
    for (std::uint32_t node = 0; node < adapter.physicalAdapterCount; ++node)
    {
      const auto read = [&](D3DKMT_MEMORY_SEGMENT_GROUP group) {
        D3DKMT_QUERYVIDEOMEMORYINFO memory{};
        memory.hProcess = process;
        memory.hAdapter = entry.hAdapter;
        memory.PhysicalAdapterIndex = node;
        memory.MemorySegmentGroup = group;
        const auto status = queryMemory(&memory);
        if (status != 0)
        {
          result.complete = false;
          return WindowsGpuMemorySegment{0, 0, 0, 0, status};
        }
        return WindowsGpuMemorySegment{memory.CurrentUsage, memory.Budget, memory.CurrentReservation,
                                       memory.AvailableForReservation, status};
      };
      adapter.nodes.push_back({node, read(D3DKMT_MEMORY_SEGMENT_GROUP_LOCAL),
                               read(D3DKMT_MEMORY_SEGMENT_GROUP_NON_LOCAL)});
    }
    adapter.closeStatus = handles.Close(entry);
    result.complete = result.complete && *adapter.typeStatus == 0 &&
        *adapter.physicalCountStatus == 0 && adapter.physicalAdapterCount != 0 &&
        adapter.closeStatus && *adapter.closeStatus == 0;
  }
#endif
  return result;
}
} // namespace cutum
