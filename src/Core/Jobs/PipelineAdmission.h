#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>

namespace cutum
{

/// Documented upper bound for one async mesh result (vertex RAM).
constexpr std::size_t kEstimatedMeshResultBytesMax = 512 * 1024;

/// Documented estimate for one captured chunk snapshot (blocks + shell).
constexpr std::size_t kEstimatedChunkSnapshotBytes = 64 * 1024;

/// M12: byte-weighted pipeline credits (snapshot / result / GPU pending).
class UPipelineAdmission
{
public:
  static UPipelineAdmission &Get();

  bool TryAcquireSnapshotBytes(std::size_t bytes);
  void ReleaseSnapshotBytes(std::size_t bytes);
  bool TryAcquireResultBytes(std::size_t bytes);
  void ReleaseResultBytes(std::size_t bytes);
  bool TryAcquireGpuPendingBytes(std::size_t bytes);
  void ReleaseGpuPendingBytes(std::size_t bytes);

  std::size_t SnapshotPendingBytes() const;
  std::size_t ResultPendingBytes() const;
  std::size_t GpuPendingBytes() const;

  void SetSnapshotCap(std::size_t cap) { SnapshotCap = cap; }
  void SetResultCap(std::size_t cap) { ResultCap = cap; }
  void SetGpuCap(std::size_t cap) { GpuCap = cap; }

private:
  std::atomic<std::size_t> SnapshotPending{0};
  std::atomic<std::size_t> ResultPending{0};
  std::atomic<std::size_t> GpuPending{0};
  std::size_t SnapshotCap{96 * 1024 * 1024};
  std::size_t ResultCap{128 * 1024 * 1024};
  std::size_t GpuCap{256 * 1024 * 1024};
};

enum class PipelineCreditKind : uint8_t
{
  Snapshot,
  Result,
  Gpu,
};

/// RAII credit guard — releases on all exit paths.
class UPipelineCreditGuard
{
public:
  UPipelineCreditGuard(PipelineCreditKind kind, std::size_t bytes, bool acquired);
  ~UPipelineCreditGuard();
  UPipelineCreditGuard(const UPipelineCreditGuard &) = delete;
  UPipelineCreditGuard &operator=(const UPipelineCreditGuard &) = delete;
  UPipelineCreditGuard(UPipelineCreditGuard &&other) noexcept;
  UPipelineCreditGuard &operator=(UPipelineCreditGuard &&other) noexcept;

  bool Held() const { return Held_; }

private:
  void Release();

  PipelineCreditKind Kind_{PipelineCreditKind::Snapshot};
  std::size_t Bytes_{0};
  bool Held_{false};
};

} // namespace cutum
