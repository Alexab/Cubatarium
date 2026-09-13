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
  return out;
}

struct OracleGateResult
{
  bool pass{true};
  size_t fault_n{0};
  size_t legal_dark_n{0};
  size_t correct_lit_n{0};
};

/// Assert each probe classifies to its expected DrawClass.
/// Reference-visible necessary draws in SmallOracleWorld must expect
/// CorrectLit or LegalDark (never Missing*/FalseNegCull).
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
      // Expected fault class matched — still counts as a fault for gate stats.
      ++r.fault_n;
      r.pass = false;
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
inline DrawClass DrawClassFromVisibleBlackCensus(VisibleBlackCause cause,
                                                 bool has_published_gpu,
                                                 bool in_pass_commands,
                                                 bool cpu_hit)
{
  const bool legal = cause == VisibleBlackCause::LegalDarkNoRepair;
  const bool light_ok = cause != VisibleBlackCause::StaleDarkWithLitField;
  return ClassifyCoord(ProbeFromCensus(/*desired*/ true, has_published_gpu,
                                       in_pass_commands, cpu_hit, light_ok,
                                       legal));
}

} // namespace cutum
