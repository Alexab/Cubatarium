#include "Core/Jobs/PipelineAdmission.h"

#include <algorithm>

namespace cutum
{

namespace
{
bool TryAcquire(std::atomic<std::size_t> &pending, std::size_t cap,
                std::size_t bytes)
{
  if (bytes == 0)
  {
    return true;
  }
  std::size_t cur = pending.load(std::memory_order_relaxed);
  while (cur + bytes <= cap)
  {
    if (pending.compare_exchange_weak(cur, cur + bytes, std::memory_order_acq_rel,
                                     std::memory_order_relaxed))
    {
      return true;
    }
  }
  return false;
}

void Release(std::atomic<std::size_t> &pending, std::size_t bytes)
{
  if (bytes == 0)
  {
    return;
  }
  std::size_t cur = pending.load(std::memory_order_relaxed);
  while (!pending.compare_exchange_weak(cur, cur >= bytes ? cur - bytes : 0,
                                      std::memory_order_acq_rel,
                                      std::memory_order_relaxed))
  {
  }
}
} // namespace

UPipelineAdmission &UPipelineAdmission::Get()
{
  static UPipelineAdmission instance;
  return instance;
}

bool UPipelineAdmission::TryAcquireSnapshotBytes(const std::size_t bytes)
{
  return TryAcquire(SnapshotPending, SnapshotCap, bytes);
}

void UPipelineAdmission::ReleaseSnapshotBytes(const std::size_t bytes)
{
  Release(SnapshotPending, bytes);
}

bool UPipelineAdmission::TryAcquireResultBytes(const std::size_t bytes)
{
  return TryAcquire(ResultPending, ResultCap, bytes);
}

void UPipelineAdmission::ReleaseResultBytes(const std::size_t bytes)
{
  Release(ResultPending, bytes);
}

bool UPipelineAdmission::TryAcquireGpuPendingBytes(const std::size_t bytes)
{
  return TryAcquire(GpuPending, GpuCap, bytes);
}

void UPipelineAdmission::ReleaseGpuPendingBytes(const std::size_t bytes)
{
  Release(GpuPending, bytes);
}

std::size_t UPipelineAdmission::SnapshotPendingBytes() const
{
  return SnapshotPending.load(std::memory_order_relaxed);
}

std::size_t UPipelineAdmission::ResultPendingBytes() const
{
  return ResultPending.load(std::memory_order_relaxed);
}

std::size_t UPipelineAdmission::GpuPendingBytes() const
{
  return GpuPending.load(std::memory_order_relaxed);
}

UPipelineCreditGuard::UPipelineCreditGuard(const PipelineCreditKind kind,
                                           const std::size_t bytes,
                                           const bool acquired)
    : Kind_(kind), Bytes_(bytes), Held_(acquired && bytes > 0)
{
}

UPipelineCreditGuard::~UPipelineCreditGuard() { Release(); }

UPipelineCreditGuard::UPipelineCreditGuard(UPipelineCreditGuard &&other) noexcept
    : Kind_(other.Kind_), Bytes_(other.Bytes_), Held_(other.Held_)
{
  other.Held_ = false;
  other.Bytes_ = 0;
}

UPipelineCreditGuard &UPipelineCreditGuard::operator=(
    UPipelineCreditGuard &&other) noexcept
{
  if (this != &other)
  {
    Release();
    Kind_ = other.Kind_;
    Bytes_ = other.Bytes_;
    Held_ = other.Held_;
    other.Held_ = false;
    other.Bytes_ = 0;
  }
  return *this;
}

void UPipelineCreditGuard::Release()
{
  if (!Held_)
  {
    return;
  }
  auto &adm = UPipelineAdmission::Get();
  switch (Kind_)
  {
  case PipelineCreditKind::Snapshot:
    adm.ReleaseSnapshotBytes(Bytes_);
    break;
  case PipelineCreditKind::Result:
    adm.ReleaseResultBytes(Bytes_);
    break;
  case PipelineCreditKind::Gpu:
    adm.ReleaseGpuPendingBytes(Bytes_);
    break;
  }
  Held_ = false;
  Bytes_ = 0;
}

} // namespace cutum
