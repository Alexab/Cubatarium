#include "World/Streaming/EnterVisualGate.h"

namespace cutum
{

void UEnterVisualGate::Reset()
{
  ItemsByColumn.clear();
  void_relight_probed_.clear();
  open_sky_applied_.clear();
  Captured = false;
  PeakDebt = 0;
  GatePhase = EnterVisualGatePhase::Idle;
  frames_without_gpu_finish_ = 0;
  ++open_sky_capture_id_;
  if (open_sky_capture_id_ == 0)
  {
    ++open_sky_capture_id_;
  }
}

void UEnterVisualGate::BeginCapture()
{
  ItemsByColumn.clear();
  void_relight_probed_.clear();
  open_sky_applied_.clear();
  Captured = false;
  PeakDebt = 0;
  GatePhase = EnterVisualGatePhase::Capture;
  frames_without_gpu_finish_ = 0;
  ++open_sky_capture_id_;
  if (open_sky_capture_id_ == 0)
  {
    ++open_sky_capture_id_;
  }
}

void UEnterVisualGate::AddCapturedColumn(glm::ivec2 col,
                                         EnterVisualItemState initial)
{
  ItemsByColumn[col] = initial;
}

void UEnterVisualGate::EndCapture()
{
  PeakDebt = static_cast<int>(ItemsByColumn.size());
  Captured = true;
  GatePhase = PeakDebt > 0 ? EnterVisualGatePhase::DrainLight
                           : EnterVisualGatePhase::Verify;
}

int UEnterVisualGate::Remaining() const
{
  int n = 0;
  for (const auto &kv : ItemsByColumn)
  {
    if (kv.second != EnterVisualItemState::Done)
    {
      ++n;
    }
  }
  return n;
}

EnterVisualItemState UEnterVisualGate::GetState(glm::ivec2 col) const
{
  const auto it = ItemsByColumn.find(col);
  if (it == ItemsByColumn.end())
  {
    return EnterVisualItemState::Done;
  }
  return it->second;
}

bool UEnterVisualGate::Contains(glm::ivec2 col) const
{
  return ItemsByColumn.find(col) != ItemsByColumn.end();
}

void UEnterVisualGate::ObserveColumn(glm::ivec2 col,
                                     EnterVisualItemState observed)
{
  auto it = ItemsByColumn.find(col);
  if (it == ItemsByColumn.end())
  {
    return;
  }
  it->second = AdvanceEnterVisualItemStateMonotonic(it->second, observed);
}

void UEnterVisualGate::MarkDone(glm::ivec2 col)
{
  auto it = ItemsByColumn.find(col);
  if (it == ItemsByColumn.end())
  {
    return;
  }
  it->second = EnterVisualItemState::Done;
}

void UEnterVisualGate::NoteGpuFinishProgress(bool any_finish)
{
  if (any_finish)
  {
    frames_without_gpu_finish_ = 0;
  }
  else
  {
    ++frames_without_gpu_finish_;
  }
}

void UEnterVisualGate::NoteVoidRelightProbed(glm::ivec2 col)
{
  void_relight_probed_[col] = 1;
}

bool UEnterVisualGate::WasVoidRelightProbed(glm::ivec2 col) const
{
  return void_relight_probed_.find(col) != void_relight_probed_.end();
}

uint64_t UEnterVisualGate::NoteOpenSkyApplied(glm::ivec2 col)
{
  if (open_sky_capture_id_ == 0)
  {
    ++open_sky_capture_id_;
  }
  open_sky_applied_[col] = open_sky_capture_id_;
  return open_sky_capture_id_;
}

bool UEnterVisualGate::WasOpenSkyApplied(glm::ivec2 col) const
{
  return open_sky_applied_.find(col) != open_sky_applied_.end();
}

uint64_t UEnterVisualGate::OpenSkyEventId(glm::ivec2 col) const
{
  const auto it = open_sky_applied_.find(col);
  return it == open_sky_applied_.end() ? 0 : it->second;
}

} // namespace cutum
