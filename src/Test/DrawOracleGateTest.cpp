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
  Expect(scene.size() == 6, "SmallOracleWorld has 6 slots");
  const auto gate = OracleGate(scene);
  Expect(gate.pass, "OracleGate PASS on canned SmallOracleWorld");
  Expect(gate.legal_dark_n == 1, "exactly one LegalDark cave");
  Expect(gate.correct_lit_n == 5, "five CorrectLit reference draws");
  Expect(gate.fault_n == 0, "no Missing*/FalseNegCull faults");

  // LegalDark ↔ attribution: no auto-relight demand storm.
  Expect(ClassifyVisibleBlackColumn(false, true, false, false, false, false) ==
             VisibleBlackCause::LegalDarkNoRepair,
         "LegalDarkNoRepair attribution");
  Expect(LegalDarkDemandConverges(0, 2), "zero repair enqueues converges");
  Expect(LegalDarkDemandConverges(2, 2), "bounded repair converges");
  Expect(!LegalDarkDemandConverges(3, 2), "unbounded repair fails gate");

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
