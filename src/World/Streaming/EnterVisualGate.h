#pragma once

#include <algorithm>
#include <cstdint>
#include <unordered_map>
#include <utility>
#include <vector>

#include <glm/glm.hpp>

namespace cutum
{

/// Era50: EnterVisualGate completion FSM — single owner for enter drain.
/// Cruise streaming is untouched; exit SoT is worklist Done + unfinished void.

enum class EnterVisualItemState : uint8_t
{
  NeedLight = 0,
  NeedRemesh = 1,
  NeedGpu = 2,
  Done = 3,
};

enum class EnterVisualGatePhase : uint8_t
{
  Idle = 0,
  Capture = 1,
  DrainLight = 2,
  DrainRemesh = 3,
  DrainGpu = 4,
  Verify = 5,
  End = 6,
};

struct EnterVisualGroundHash
{
  std::size_t operator()(const glm::ivec2 &v) const noexcept
  {
    return static_cast<std::size_t>(
        (static_cast<uint64_t>(static_cast<uint32_t>(v.x)) << 32) ^
        static_cast<uint32_t>(v.y));
  }
};

/// Era50: EnterLitQuiesce only when worklist remaining==0 (not whole gate).
inline bool EnterLitQuiesceAllowed(bool enter_gate_active,
                                   int worklist_remaining)
{
  return enter_gate_active && worklist_remaining <= 0;
}

/// Era50: EnterGpuQuiesceDrain stays on for the whole enter gate.
inline bool EnterGpuQuiesceDrainAllowed(bool enter_gate_active)
{
  return enter_gate_active;
}

/// Era50: unfinished void for exit — SoftDefer/placeholder faces do not block.
inline int EnterVisibilityUnfinishedVoid(int dark_face_void_near_n,
                                         int soft_defer_placeholder_n)
{
  return std::max(0, dark_face_void_near_n -
                         std::max(0, soft_defer_placeholder_n));
}

/// Era50: void-edge FullyDark (LitReady, zero light field) may terminate via
/// SoftDefer + FirstMesh ticket — Relight cannot invent light.
inline bool EnterVisualVoidEdgeAcceptsSoftDefer(bool lit_ready, bool pending,
                                                bool fully_dark,
                                                bool stale_lit_field,
                                                bool soft_defer_with_ticket)
{
  if (!fully_dark || pending || !lit_ready || stale_lit_field)
  {
    return false;
  }
  return soft_defer_with_ticket;
}

/// Classify per-column pipeline stage from live evidence (capture / refresh).
inline EnterVisualItemState ClassifyEnterVisualItemState(
    bool pending_or_unlit, bool fully_dark_stale_needs_remesh,
    bool gpu_pending_or_inflight, bool terminal_ready)
{
  if (terminal_ready)
  {
    return EnterVisualItemState::Done;
  }
  if (pending_or_unlit)
  {
    return EnterVisualItemState::NeedLight;
  }
  if (fully_dark_stale_needs_remesh)
  {
    return EnterVisualItemState::NeedRemesh;
  }
  if (gpu_pending_or_inflight)
  {
    return EnterVisualItemState::NeedGpu;
  }
  // Void-edge / SoftDefer path still unfinished until ticket accepted.
  return EnterVisualItemState::NeedRemesh;
}

/// Monotonic: once Done, stay Done for debt (no live recount growth).
inline EnterVisualItemState AdvanceEnterVisualItemStateMonotonic(
    EnterVisualItemState prev, EnterVisualItemState observed)
{
  if (prev == EnterVisualItemState::Done)
  {
    return EnterVisualItemState::Done;
  }
  if (observed == EnterVisualItemState::Done)
  {
    return EnterVisualItemState::Done;
  }
  // Allow forward progress only (NeedLight → Remesh → Gpu → Done).
  return static_cast<EnterVisualItemState>(
      std::max(static_cast<uint8_t>(prev), static_cast<uint8_t>(observed)));
}

/// Era50: escalate GPU drain when worklist stuck with pending GPU (no finish).
inline bool ShouldEscalateEnterWorklistGpuDrain(bool gate_active, int remaining,
                                                int gpu_pending_near,
                                                int frames_without_gpu_finish,
                                                int stall_frames = 90)
{
  return gate_active && remaining > 0 && gpu_pending_near > 0 &&
         frames_without_gpu_finish >= stall_frames;
}

/// Pure void-edge repair: OpenSky+relight first; remesh only after OpenSky.
enum class EnterVoidEdgeAction : uint8_t
{
  None = 0,
  RemeshStale = 1,
  SoftDeferTicket = 2,
  RelightOnce = 3,
};

inline EnterVoidEdgeAction ClassifyEnterVoidEdgeAction(
    bool fully_dark, bool stale_lit_field, bool /*soft_defer_with_ticket*/,
    bool relight_already_owned, bool open_sky_done)
{
  if (!fully_dark)
  {
    return EnterVoidEdgeAction::None;
  }
  if (!open_sky_done)
  {
    return EnterVoidEdgeAction::RelightOnce;
  }
  if (relight_already_owned)
  {
    return EnterVoidEdgeAction::None;
  }
  if (stale_lit_field)
  {
    return EnterVoidEdgeAction::RemeshStale;
  }
  return EnterVoidEdgeAction::None;
}

/// Era51: missing neighbor under enter gate ⇒ treat as open daytime sky.
inline bool ShouldTreatMissingNeighborAsOpenSky(bool neighbor_loaded,
                                                bool enter_gate_active,
                                                bool include_skylight)
{
  return enter_gate_active && include_skylight && !neighbor_loaded;
}

class EnterVisualGate
{
public:
  void Reset();
  void BeginCapture();
  void AddCapturedColumn(glm::ivec2 col, EnterVisualItemState initial);
  void EndCapture();

  bool IsCaptured() const { return captured_; }
  int Peak() const { return peak_; }
  int Remaining() const;
  EnterVisualGatePhase Phase() const { return phase_; }
  void SetPhase(EnterVisualGatePhase phase) { phase_ = phase; }

  EnterVisualItemState GetState(glm::ivec2 col) const;
  bool Contains(glm::ivec2 col) const;
  void ObserveColumn(glm::ivec2 col, EnterVisualItemState observed);
  void MarkDone(glm::ivec2 col);

  const std::unordered_map<glm::ivec2, EnterVisualItemState,
                           EnterVisualGroundHash> &
  Items() const
  {
    return items_;
  }

  int FramesWithoutGpuFinish() const { return frames_without_gpu_finish_; }
  void NoteGpuFinishProgress(bool any_finish);
  void NoteVoidRelightProbed(glm::ivec2 col);
  bool WasVoidRelightProbed(glm::ivec2 col) const;
  void NoteOpenSkyApplied(glm::ivec2 col);
  bool WasOpenSkyApplied(glm::ivec2 col) const;

private:
  std::unordered_map<glm::ivec2, EnterVisualItemState, EnterVisualGroundHash>
      items_;
  std::unordered_map<glm::ivec2, uint8_t, EnterVisualGroundHash>
      void_relight_probed_;
  std::unordered_map<glm::ivec2, uint8_t, EnterVisualGroundHash> open_sky_applied_;
  bool captured_{false};
  int peak_{0};
  EnterVisualGatePhase phase_{EnterVisualGatePhase::Idle};
  int frames_without_gpu_finish_{0};
};

} // namespace cutum
