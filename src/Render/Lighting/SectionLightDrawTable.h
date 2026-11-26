#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace cutum
{
/// Per-pass artifact handles, indexed by DrawElementsIndirect.baseInstance.
/// Two fenced stores bound upload memory. A busy/failed update requires the
/// caller's ordered uniform fallback, never drawing obsolete row membership.
class SectionLightDrawTable
{
public:
  using Words = std::array<uint32_t, 4>;
  static constexpr size_t MaxRows = 65536;
  SectionLightDrawTable() = default;
  ~SectionLightDrawTable();
  SectionLightDrawTable(const SectionLightDrawTable &) = delete;
  SectionLightDrawTable &operator=(const SectionLightDrawTable &) = delete;
  bool Prepare(const std::vector<Words> &rows);
  bool BindAttribute(unsigned location = 7) const;
  static void DisableAttribute(unsigned location = 7);
  void SignalDrawComplete();
  void Reset();
  unsigned Buffer() const;
  void InspectGpuBuffers(class TerrainGpuBufferCensus &out) const;
  uint64_t UploadCount() const { return Uploads; }

private:
  friend struct SectionLightDrawTableTestAccess;
  struct Store
  {
    unsigned buffer{0};
    void *fence{nullptr};
    bool unfenced{false};
    std::vector<Words> rows;
  } Stores[2];
  int Live{-1};
  uint64_t Uploads{0};
  void (*BeforeTransfer)(void *){nullptr};
  void *TransferContext{nullptr};
};
} // namespace cutum
