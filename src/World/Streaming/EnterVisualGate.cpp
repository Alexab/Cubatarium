#include "World/Streaming/EnterVisualGate.h"

namespace cutum
{

void EnterVisualGate::Reset()
{
  items_.clear();
  void_relight_probed_.clear();
  captured_ = false;
  peak_ = 0;
  phase_ = EnterVisualGatePhase::Idle;
  frames_without_gpu_finish_ = 0;
}

void EnterVisualGate::BeginCapture()
{
  items_.clear();
  void_relight_probed_.clear();
  captured_ = false;
  peak_ = 0;
  phase_ = EnterVisualGatePhase::Capture;
  frames_without_gpu_finish_ = 0;
}

void EnterVisualGate::AddCapturedColumn(glm::ivec2 col,
                                        EnterVisualItemState initial)
{
  items_[col] = initial;
}

void EnterVisualGate::EndCapture()
{
  peak_ = static_cast<int>(items_.size());
  captured_ = true;
  phase_ = peak_ > 0 ? EnterVisualGatePhase::DrainLight
                     : EnterVisualGatePhase::Verify;
}

int EnterVisualGate::Remaining() const
{
  int n = 0;
  for (const auto &kv : items_)
  {
    if (kv.second != EnterVisualItemState::Done)
    {
      ++n;
    }
  }
  return n;
}

EnterVisualItemState EnterVisualGate::GetState(glm::ivec2 col) const
{
  const auto it = items_.find(col);
  if (it == items_.end())
  {
    return EnterVisualItemState::Done;
  }
  return it->second;
}

bool EnterVisualGate::Contains(glm::ivec2 col) const
{
  return items_.find(col) != items_.end();
}

void EnterVisualGate::ObserveColumn(glm::ivec2 col,
                                    EnterVisualItemState observed)
{
  auto it = items_.find(col);
  if (it == items_.end())
  {
    return;
  }
  it->second = AdvanceEnterVisualItemStateMonotonic(it->second, observed);
}

void EnterVisualGate::MarkDone(glm::ivec2 col)
{
  auto it = items_.find(col);
  if (it == items_.end())
  {
    return;
  }
  it->second = EnterVisualItemState::Done;
}

void EnterVisualGate::NoteGpuFinishProgress(bool any_finish)
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

void EnterVisualGate::NoteVoidRelightProbed(glm::ivec2 col)
{
  void_relight_probed_[col] = 1;
}

bool EnterVisualGate::WasVoidRelightProbed(glm::ivec2 col) const
{
  return void_relight_probed_.find(col) != void_relight_probed_.end();
}

} // namespace cutum
