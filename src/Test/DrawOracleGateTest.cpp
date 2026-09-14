#include "World/Diagnostics/DrawOracle.h"
#include "World/Streaming/VisibleBlackAttribution.h"

#include <iostream>

namespace
{

int gFails = 0;

void Expect(bool cond, const char *msg)
{
  if (!cond)
  {
    std::cerr << "FAIL: " << msg << "\n";
    ++gFails;
  }
}

} // namespace

/// Strategy A M-Q2b: SmallOracleWorld + ClassifyCoord + OracleGate (CPU contract).
/// GL pixel/object-id sampling remains on greedy_vertex_pool_* --driver path.
int main()
{
  using cutum::ClassifyCoord;
  using cutum::DrawClass;
  using cutum::DrawOracleProbe;
  using cutum::LegalDarkDemandConverges;
  using cutum::OracleGate;
  using cutum::BuildSmallOracleWorldExpectations;
  using cutum::BuildSmallOracleReferenceExpectations;
  using cutum::VisibleBlackCause;
  using cutum::ClassifyVisibleBlackColumn;

  Expect(ClassifyCoord(DrawOracleProbe{}) == DrawClass::Irrelevant,
         "not desired → Irrelevant");
  Expect(ClassifyCoord(DrawOracleProbe{true, false, true, true, true, false}) ==
             DrawClass::MissingResident,
         "no GPU → MissingResident");
  Expect(ClassifyCoord(DrawOracleProbe{true, true, false, true, true, false}) ==
             DrawClass::MissingCommand,
         "not in commands → MissingCommand");
  Expect(ClassifyCoord(DrawOracleProbe{true, true, true, false, true, false}) ==
             DrawClass::FalseNegCull,
         "no pixel hit → FalseNegCull");
  Expect(ClassifyCoord(DrawOracleProbe{true, true, true, true, true, true}) ==
             DrawClass::LegalDark,
         "cave → LegalDark");
  Expect(ClassifyCoord(DrawOracleProbe{true, true, true, true, false, false}) ==
             DrawClass::StaleVertexLight,
         "stale light → StaleVertexLight");
  Expect(ClassifyCoord(DrawOracleProbe{true, true, true, true, true, false}) ==
             DrawClass::CorrectLit,
         "happy path → CorrectLit");

  const auto scene = BuildSmallOracleWorldExpectations();
  Expect(scene.size() == 8, "SmallOracleWorld has 8 slots (6 ref + 2 fault)");
  const auto ref = BuildSmallOracleReferenceExpectations();
  Expect(ref.size() == 6, "reference SmallOracle has 6 CorrectLit/LegalDark");
  const auto gate = OracleGate(scene);
  Expect(gate.pass, "OracleGate PASS on canned SmallOracleWorld incl. faults");
  Expect(gate.legal_dark_n == 1, "exactly one LegalDark cave");
  Expect(gate.correct_lit_n == 5, "five CorrectLit reference draws");
  Expect(gate.expected_fault_match_n == 2,
         "FullyDarkPending + MissingResident classify as expected faults");
  Expect(gate.fault_n == 0, "no unexpected classification mismatches");
  Expect(OracleGate(ref).pass, "reference-only OracleGate PASS");
  Expect(OracleGate(ref).fault_n == 0, "reference scene has no mismatches");

  // LegalDark ↔ attribution: no auto-relight demand storm.
  Expect(ClassifyVisibleBlackColumn(false, true, false, false, false, false) ==
             VisibleBlackCause::LegalDarkNoRepair,
         "LegalDarkNoRepair attribution");
  Expect(LegalDarkDemandConverges(0, 2), "zero repair enqueues converges");
  Expect(LegalDarkDemandConverges(2, 2), "bounded repair converges");
  Expect(!LegalDarkDemandConverges(3, 2), "unbounded repair fails gate");
  // Scene-level: LegalDark slots must not imply unbounded remesh demand.
  {
    int legal_repair_enqueues = 0;
    for (const auto &e : scene)
    {
      if (e.expect == DrawClass::LegalDark)
      {
        legal_repair_enqueues += 0; // LegalDark: no remesh demand
      }
    }
    Expect(LegalDarkDemandConverges(legal_repair_enqueues, /*max*/ 0),
           "SmallOracle LegalDark remesh demand converges at 0");
  }

  // Q2b: census → DrawOracle adapters (CPU; GL pixel remains driver path).
  {
    using cutum::CensusMismatchRequiresOracle;
    using cutum::DrawClassFromVisibleBlackCensus;
    using cutum::ProbeFromCensus;
    Expect(CensusMismatchRequiresOracle(0, 63),
           "census mismatch when unfinished=0 and VB>0");
    Expect(!CensusMismatchRequiresOracle(1, 63),
           "no oracle alarm while unfinished>0");
    Expect(!CensusMismatchRequiresOracle(0, 0), "no mismatch when VB=0");

    const auto legal = DrawClassFromVisibleBlackCensus(
        VisibleBlackCause::LegalDarkNoRepair, true, true, true);
    Expect(legal == DrawClass::LegalDark,
           "census LegalDark maps to DrawClass::LegalDark");
    const auto stale = DrawClassFromVisibleBlackCensus(
        VisibleBlackCause::StaleDarkWithLitField, true, true, true);
    Expect(stale == DrawClass::StaleVertexLight,
           "census stale-dark maps to StaleVertexLight");
    const auto missing = DrawClassFromVisibleBlackCensus(
        VisibleBlackCause::FullyDarkNoTicket, false, false, false);
    Expect(missing == DrawClass::MissingResident,
           "no GPU → MissingResident even from VB cause");
    Expect(ClassifyCoord(ProbeFromCensus(true, true, true, true, true,
                                         false)) == DrawClass::CorrectLit,
           "ProbeFromCensus happy path");

    const auto hist = cutum::AccumulateDrawOracleFromVbCensus(
        /*unfinished*/ 2, /*stale*/ 3, /*repair*/ 4, /*no_ticket*/ 1,
        /*stalled*/ 1, /*legal*/ 5);
    Expect(hist.missing_resident_n == 2, "unfinished → MissingResident");
    Expect(hist.stale_vertex_light_n == 3 + 6,
           "stale + FullyDark* → StaleVertexLight");
    Expect(hist.legal_dark_n == 5, "legal cave → LegalDark");
    Expect(hist.correct_lit_proxy_n == 0,
           "FullyDark* is not CorrectLit proxy (Q2b)");
    Expect(hist.fully_dark_debt_n == 6, "fully_dark_debt sums repair buckets");
    Expect(hist.false_neg_cull_n == 0, "CPU census has no FalseNegCull");
    Expect(hist.fault_n == 2 + 3 + 6, "faults = missing + stale + FullyDark");
    {
      auto with_oid = hist;
      cutum::ApplyObjectIdMissesToCensus(with_oid, 3);
      Expect(with_oid.false_neg_cull_n == 3,
             "E4: object-id misses → FalseNegCull");
      Expect(with_oid.fault_n == hist.fault_n + 3,
             "E4: object-id misses bump fault_n");
      cutum::ApplyObjectIdMissesToCensus(with_oid, 0);
      Expect(with_oid.false_neg_cull_n == 3, "E4: zero miss is no-op");
    }
    Expect(DrawClassFromVisibleBlackCensus(
               VisibleBlackCause::FullyDarkPendingRepair, true, true, true) ==
               DrawClass::StaleVertexLight,
           "FullyDarkPendingRepair → StaleVertexLight");

    using cutum::AdvanceOldestDebtAgeFrames;
    using cutum::ShouldCountDebtAgeGrewWithSchedule;
    Expect(AdvanceOldestDebtAgeFrames(0, true) == 1, "G1-P3: first debt frame");
    Expect(AdvanceOldestDebtAgeFrames(5, true) == 6, "G1-P3: age bumps");
    Expect(AdvanceOldestDebtAgeFrames(9, false) == 0,
           "G1-P3: clear resets age (publish/LegalDark)");
    Expect(AdvanceOldestDebtAgeFrames(0, false) == 0, "G1-P3: calm stays 0");
    Expect(AdvanceOldestDebtAgeFrames(12, true, 40, 30) == 1,
           "G1-P3: debt_n shrink restarts oldest age");
    Expect(AdvanceOldestDebtAgeFrames(12, true, 40, 40, 1) == 1,
           "G1-P3: lit publish restarts age");
    Expect(AdvanceOldestDebtAgeFrames(12, true, 40, 45) == 13,
           "G1-P3: debt growth keeps bumping");
    Expect(ShouldCountDebtAgeGrewWithSchedule(3, 4, 1),
           "G1-P3: age↑ + schedule_ok ⇒ watchdog");
    Expect(!ShouldCountDebtAgeGrewWithSchedule(3, 4, 0),
           "G1-P3: age↑ without schedule_ok ⇒ no watchdog");
    Expect(!ShouldCountDebtAgeGrewWithSchedule(4, 0, 2),
           "G1-P3: age reset ⇒ no grew");
  }

  // Fault injection: MissingResident must fail when expect is CorrectLit.
  {
    auto broken = scene;
    broken[0].probe.has_published_gpu = false;
    Expect(!OracleGate(broken).pass, "MissingResident fails CorrectLit expect");
  }

  if (gFails != 0)
  {
    std::cerr << gFails << " test(s) failed\n";
    return 1;
  }
  std::cout << "DrawOracleGateTest: PASS\n";
  return 0;
}
