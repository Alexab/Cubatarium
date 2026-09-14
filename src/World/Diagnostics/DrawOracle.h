#pragma once

#include "World/Streaming/VisibleBlackAttribution.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace cutum
{

/// Q2b / Strategy A Phase 3a: per-coordinate draw classification for oracle gates.
enum class DrawClass : uint8_t
{
  Irrelevant = 0,
  MissingResident,
  MissingCommand,
  FalseNegCull,
  StaleVertexLight,
  LegalDark,
  CorrectLit,
};

/// Probe inputs for one chunk coord (filled by CPU reference + GPU/census hooks).
struct DrawOracleProbe
{
  bool desired_visible{false};
  bool has_published_gpu{false};
  bool in_pass_commands{false};
  bool pixel_or_object_hit{false};
  bool light_revision_ok{true};
  bool cave_legal_dark{false};
};

inline DrawClass ClassifyCoord(const DrawOracleProbe &p)
{
  if (!p.desired_visible)
  {
    return DrawClass::Irrelevant;
  }
  if (!p.has_published_gpu)
  {
    return DrawClass::MissingResident;
  }
  if (!p.in_pass_commands)
  {
    return DrawClass::MissingCommand;
  }
  if (!p.pixel_or_object_hit)
  {
    return DrawClass::FalseNegCull;
  }
  if (p.cave_legal_dark)
  {
    return DrawClass::LegalDark;
  }
  if (!p.light_revision_ok)
  {
    return DrawClass::StaleVertexLight;
  }
  return DrawClass::CorrectLit;
}

inline bool DrawClassIsFault(DrawClass c)
{
  return c == DrawClass::MissingResident || c == DrawClass::MissingCommand ||
         c == DrawClass::FalseNegCull || c == DrawClass::StaleVertexLight;
}

/// Minimal deterministic scene slots for SmallOracleWorld (no full save).
enum class SmallOracleSlot : uint8_t
{
  OpaqueLit = 0,
  TransparentLit,
  DecorFluid,
  OverhangSideLight,
  ClosedCaveLegalDark,
  ChunkBoundarySeam,
  /// G1-P4 class coverage (fault + legal) — not part of reference CorrectLit set.
  FullyDarkPending,
  MissingResidentHole,
  Count,
};

struct SmallOracleExpected
{
  SmallOracleSlot slot{SmallOracleSlot::OpaqueLit};
  DrawOracleProbe probe{};
  DrawClass expect{DrawClass::CorrectLit};
};

/// Build the Strategy A SmallOracleWorld expectation table.
/// Callers that have real GPU/census fill `probe` fields; unit tests use the
/// canned probes below to lock ClassifyCoord / OracleGate contracts.
inline std::vector<SmallOracleExpected> BuildSmallOracleWorldExpectations()
{
  std::vector<SmallOracleExpected> out;
  out.reserve(static_cast<size_t>(SmallOracleSlot::Count));

  auto add = [&](SmallOracleSlot slot, DrawOracleProbe probe, DrawClass expect)
  {
    out.push_back(SmallOracleExpected{slot, probe, expect});
  };

  add(SmallOracleSlot::OpaqueLit,
      DrawOracleProbe{true, true, true, true, true, false},
      DrawClass::CorrectLit);
  add(SmallOracleSlot::TransparentLit,
      DrawOracleProbe{true, true, true, true, true, false},
      DrawClass::CorrectLit);
  add(SmallOracleSlot::DecorFluid,
      DrawOracleProbe{true, true, true, true, true, false},
      DrawClass::CorrectLit);
  add(SmallOracleSlot::OverhangSideLight,
      DrawOracleProbe{true, true, true, true, true, false},
      DrawClass::CorrectLit);
  add(SmallOracleSlot::ClosedCaveLegalDark,
      DrawOracleProbe{true, true, true, true, true, true},
      DrawClass::LegalDark);
  add(SmallOracleSlot::ChunkBoundarySeam,
      DrawOracleProbe{true, true, true, true, true, false},
      DrawClass::CorrectLit);
  add(SmallOracleSlot::FullyDarkPending,
      DrawOracleProbe{true, true, true, true, false, false},
      DrawClass::StaleVertexLight);
  add(SmallOracleSlot::MissingResidentHole,
      DrawOracleProbe{true, false, true, true, true, false},
      DrawClass::MissingResident);
  return out;
}

/// Reference-only SmallOracleWorld (CorrectLit + LegalDark) — no fault slots.
inline std::vector<SmallOracleExpected> BuildSmallOracleReferenceExpectations()
{
  std::vector<SmallOracleExpected> out;
  for (const auto &e : BuildSmallOracleWorldExpectations())
  {
    if (e.expect == DrawClass::CorrectLit || e.expect == DrawClass::LegalDark)
    {
      out.push_back(e);
    }
  }
  return out;
}

struct OracleGateResult
{
  bool pass{true};
  size_t fault_n{0};
  size_t legal_dark_n{0};
  size_t correct_lit_n{0};
  size_t expected_fault_match_n{0};
};

/// Assert each probe classifies to its expected DrawClass.
/// Reference-visible necessary draws in SmallOracleWorld must expect
/// CorrectLit or LegalDark (never Missing*/FalseNegCull). Expected fault
/// fixtures may list StaleVertexLight / MissingResident and still PASS.
inline OracleGateResult OracleGate(const std::vector<SmallOracleExpected> &scene)
{
  OracleGateResult r;
  for (const auto &e : scene)
  {
    const DrawClass got = ClassifyCoord(e.probe);
    if (got != e.expect)
    {
      r.pass = false;
      ++r.fault_n;
      continue;
    }
    if (got == DrawClass::LegalDark)
    {
      ++r.legal_dark_n;
    }
    else if (got == DrawClass::CorrectLit)
    {
      ++r.correct_lit_n;
    }
    else if (DrawClassIsFault(got))
    {
      ++r.expected_fault_match_n;
    }
  }
  return r;
}

/// LegalDark must not enqueue infinite repair: demand stays finite over N ticks.
inline bool LegalDarkDemandConverges(int enqueue_attempts, int max_allowed)
{
  return enqueue_attempts >= 0 && enqueue_attempts <= max_allowed;
}

/// Census mismatch (WorldStreaming): unfinished_visual==0 && VB focus > 0.
inline bool CensusMismatchRequiresOracle(int unfinished_visual,
                                         int visible_black_focus_n)
{
  return unfinished_visual == 0 && visible_black_focus_n > 0;
}

/// CPU census → DrawOracleProbe. When GL pixel is unavailable, pass
/// `cpu_frustum_or_command_hit` as the pixel stand-in (resident+command path).
inline DrawOracleProbe ProbeFromCensus(bool desired_visible,
                                       bool has_published_gpu,
                                       bool in_pass_commands,
                                       bool cpu_frustum_or_command_hit,
                                       bool light_revision_ok,
                                       bool cave_legal_dark)
{
  DrawOracleProbe p;
  p.desired_visible = desired_visible;
  p.has_published_gpu = has_published_gpu;
  p.in_pass_commands = in_pass_commands;
  p.pixel_or_object_hit = cpu_frustum_or_command_hit;
  p.light_revision_ok = light_revision_ok;
  p.cave_legal_dark = cave_legal_dark;
  return p;
}

/// Map VisibleBlackCause + residency into DrawClass for census→oracle bridge.
/// Published VB columns (unfinished==0) are never MissingResident.
/// FullyDark* is StaleVertexLight (G1 fault): published dark mesh needing remesh,
/// not CorrectLit and not LegalDark (AUDIT Q2 / inv.6).
inline DrawClass DrawClassFromVisibleBlackCensus(VisibleBlackCause cause,
                                                 bool has_published_gpu,
                                                 bool in_pass_commands,
                                                 bool cpu_hit)
{
  const bool legal = cause == VisibleBlackCause::LegalDarkNoRepair;
  const bool light_ok =
      cause != VisibleBlackCause::StaleDarkWithLitField &&
      cause != VisibleBlackCause::FullyDarkPendingRepair &&
      cause != VisibleBlackCause::FullyDarkNoTicket &&
      cause != VisibleBlackCause::FullyDarkStalledTicket;
  return ClassifyCoord(ProbeFromCensus(/*desired*/ true, has_published_gpu,
                                       in_pass_commands, cpu_hit, light_ok,
                                       legal));
}

/// Aggregated DrawClass histogram from focus census (CPU stand-in for G1).
/// VB columns are treated as published+commanded; FalseNegCull stays 0 until
/// GL pixel/object-id fills `pixel_or_object_hit=false` cases.
struct DrawOracleCensusCounts
{
  int missing_resident_n{0};
  int missing_command_n{0};
  int false_neg_cull_n{0};
  int stale_vertex_light_n{0};
  int legal_dark_n{0};
  int correct_lit_proxy_n{0};
  /// FullyDark* VB columns (published dark pending/stalled repair) — G1 debt.
  int fully_dark_debt_n{0};
  int fault_n{0};
};

/// E4: fold world object-id / pixel misses into census (FalseNegCull).
/// Production mask generation still open; call sites may pass 0 until FBO
/// object-id is wired. Keeps fault accounting ready for non-zero samples.
inline void ApplyObjectIdMissesToCensus(DrawOracleCensusCounts &out,
                                        int object_id_miss_n)
{
  if (object_id_miss_n <= 0)
  {
    return;
  }
  out.false_neg_cull_n += object_id_miss_n;
  out.fault_n += object_id_miss_n;
}

inline DrawOracleCensusCounts AccumulateDrawOracleFromVbCensus(
    int unfinished_visual, int stale_lit_n, int fully_dark_repair_n,
    int fully_dark_no_ticket_n, int fully_dark_stalled_n, int legal_dark_n)
{
  DrawOracleCensusCounts out;
  out.missing_resident_n = unfinished_visual > 0 ? unfinished_visual : 0;
  out.fully_dark_debt_n =
      (fully_dark_repair_n > 0 ? fully_dark_repair_n : 0) +
      (fully_dark_no_ticket_n > 0 ? fully_dark_no_ticket_n : 0) +
      (fully_dark_stalled_n > 0 ? fully_dark_stalled_n : 0);
  auto bump = [&](VisibleBlackCause cause, int n)
  {
    if (n <= 0)
    {
      return;
    }
    // VB census columns already have drawable mesh → has_gpu + in_commands.
    const DrawClass c =
        DrawClassFromVisibleBlackCensus(cause, true, true, true);
    switch (c)
    {
    case DrawClass::MissingResident:
      out.missing_resident_n += n;
      break;
    case DrawClass::MissingCommand:
      out.missing_command_n += n;
      break;
    case DrawClass::FalseNegCull:
      out.false_neg_cull_n += n;
      break;
    case DrawClass::StaleVertexLight:
      out.stale_vertex_light_n += n;
      break;
    case DrawClass::LegalDark:
      out.legal_dark_n += n;
      break;
    case DrawClass::CorrectLit:
      out.correct_lit_proxy_n += n;
      break;
    case DrawClass::Irrelevant:
      break;
    }
  };
  bump(VisibleBlackCause::StaleDarkWithLitField, stale_lit_n);
  bump(VisibleBlackCause::FullyDarkPendingRepair, fully_dark_repair_n);
  bump(VisibleBlackCause::FullyDarkNoTicket, fully_dark_no_ticket_n);
  bump(VisibleBlackCause::FullyDarkStalledTicket, fully_dark_stalled_n);
  bump(VisibleBlackCause::LegalDarkNoRepair, legal_dark_n);
  // FullyDark* → StaleVertexLight; keep fully_dark_debt as explicit G1 side-band.
  out.fault_n = out.missing_resident_n + out.missing_command_n +
                out.false_neg_cull_n + out.stale_vertex_light_n;
  return out;
}

/// G1-P3 / A10: oldest-debt age (frames). Resets when class debt clears or
/// shrinks (Missing→drawable / lit publish / LegalDark reclass). Note/Capture
/// alone must not zero age while debt_n is unchanged/nondecreasing.
inline int AdvanceOldestDebtAgeFrames(int prev_age, bool debt_present,
                                      int prev_debt_n = -1, int debt_n = -1,
                                      int lit_publish_or_clear_n = 0)
{
  if (!debt_present || debt_n == 0)
  {
    return 0;
  }
  if (lit_publish_or_clear_n > 0)
  {
    return 1;
  }
  if (prev_debt_n >= 0 && debt_n >= 0 && debt_n < prev_debt_n)
  {
    return 1; // oldest (or some) debt cleared — restart for remaining
  }
  return prev_age < 0 ? 1 : prev_age + 1;
}

/// Watchdog: age grew while schedule_ok>0 without debt clear (no publish progress).
inline bool ShouldCountDebtAgeGrewWithSchedule(int prev_age, int new_age,
                                               int schedule_ok_n)
{
  return new_age > prev_age && schedule_ok_n > 0;
}

} // namespace cutum
