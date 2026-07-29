#include "World/Streaming/MeshLitGate.h"
#include "World/Streaming/ColumnRenderablePolicy.h"
#include "World/Streaming/ColumnFlowScheduler.h"

#include <cstdlib>
#include <iostream>
#include <vector>

using cutum::ShouldRejectDarkMeshCommit;
using cutum::SoftDeferMeshUntilLitPolicy;
using cutum::AllowUnlitFirstMesh;
using cutum::ClassifyStickyStaleDarkSoT;
using cutum::ColumnSoTKind;
using cutum::EnqueueStickyStaleRepairTickets;
using cutum::UColumnFlowScheduler;
using cutum::ColumnWorkKind;

static int failures = 0;

static void Expect(bool cond, const char *msg)
{
  if (!cond)
  {
    std::cerr << "FAIL: " << msg << std::endl;
    ++failures;
  }
}

int main()
{
  // SoftDefer: first mesh while PendingLight is deferred unless SoT
  // allow_unlit_first_mesh. Remesh while pending stays deferred.
  Expect(SoftDeferMeshUntilLitPolicy(true, false, true, true, false, false),
         "underfeet missing+pending must defer (no unlit allow)");
  Expect(SoftDeferMeshUntilLitPolicy(true, true, true, true, false, false),
         "underfeet has_mesh+pending defer remesh");
  Expect(!SoftDeferMeshUntilLitPolicy(true, true, false, true, false, false),
         "underfeet has_mesh+lit allow remesh");
  Expect(!SoftDeferMeshUntilLitPolicy(true, false, false, true, false, false),
         "underfeet missing+lit allow first mesh");

  // Focus missing + pending => defer without UnlitFirstMesh allow.
  Expect(SoftDeferMeshUntilLitPolicy(false, false, true, true, true, false),
         "focus missing+pending must defer");
  // Focus missing + lit => allow.
  Expect(!SoftDeferMeshUntilLitPolicy(false, false, false, true, true, false),
         "focus missing+lit allow");

  // Remesh of existing while pending => defer (even with unlit allow).
  Expect(SoftDeferMeshUntilLitPolicy(false, true, true, true, true, true),
         "focus has_mesh+pending defer remesh despite unlit allow");
  Expect(!SoftDeferMeshUntilLitPolicy(false, true, false, true, true, false),
         "focus has_mesh+lit allow remesh");

  // Outside focus: pending always defers; else MayMesh gate.
  Expect(SoftDeferMeshUntilLitPolicy(false, false, true, false, true, false),
         "outside missing+pending must defer");
  Expect(SoftDeferMeshUntilLitPolicy(false, false, false, false, false, false),
         "outside missing !may_mesh defer");
  Expect(!SoftDeferMeshUntilLitPolicy(false, false, false, false, true, false),
         "outside missing may_mesh allow");

  Expect(!ShouldRejectDarkMeshCommit(false, true, true),
         "lit new mesh always commits");
  Expect(ShouldRejectDarkMeshCommit(true, true, false),
         "pending-light dark first mesh rejected");
  Expect(ShouldRejectDarkMeshCommit(true, false, true),
         "dark remesh must not replace lit mesh");
  Expect(!ShouldRejectDarkMeshCommit(true, false, false),
         "cave/unlit first mesh allowed when not deferred");

  // SoT AllowUnlitFirstMesh + SoftDefer allow_unlit_first_mesh.
  Expect(AllowUnlitFirstMesh(false, 2, false, true),
         "missing r<=3 in focus → AllowUnlitFirstMesh");
  Expect(AllowUnlitFirstMesh(false, 5, true, true),
         "nearest missing beyond r3 → AllowUnlitFirstMesh");
  Expect(!AllowUnlitFirstMesh(true, 1, true, true),
         "has_mesh never AllowUnlitFirstMesh");
  Expect(!AllowUnlitFirstMesh(false, 5, false, true),
         "far non-nearest missing → no AllowUnlitFirstMesh");
  Expect(!SoftDeferMeshUntilLitPolicy(false, false, true, true, true, true),
         "policy: pending+allow_unlit_first_mesh allows first mesh");
  Expect(SoftDeferMeshUntilLitPolicy(false, false, true, true, true, false),
         "policy: pending without allow still defers");

  // TD-ARCH-026: SoT sticky/stale-dark (real invariants, not Expect(true)).
  {
    const auto no_mesh_sticky =
        ClassifyStickyStaleDarkSoT(/*has_mesh=*/false, /*sticky=*/true,
                                   /*stale=*/false, /*horiz=*/3);
    Expect(no_mesh_sticky.kind == ColumnSoTKind::StickyRemesh,
           "sticky without mesh → StickyRemesh");
    Expect(!no_mesh_sticky.draw_ok, "sticky without mesh → hide (no draw_ok)");
    Expect(no_mesh_sticky.has_repair_ticket,
           "sticky without mesh → has_repair_ticket");

    const auto meshed_sticky =
        ClassifyStickyStaleDarkSoT(true, true, false, 3);
    Expect(meshed_sticky.draw_ok && meshed_sticky.has_repair_ticket,
           "meshed sticky → draw_ok + repair ticket");

    const auto stale =
        ClassifyStickyStaleDarkSoT(true, false, true, 3);
    Expect(stale.kind == ColumnSoTKind::StaleDark, "stale-dark kind");
    Expect(stale.draw_ok && stale.has_repair_ticket,
           "meshed stale-dark → draw_ok + repair ticket");

    const auto near = ClassifyStickyStaleDarkSoT(false, true, false, 1);
    Expect(near.kind == ColumnSoTKind::None,
           "near ring uses other path (SoT sticky classifier idle)");
  }

  // Hide/sticky/stale without mesh ⇒ scheduler contains RemeshSeam|RelightThenMesh.
  {
    UColumnFlowScheduler sched;
    const glm::ivec2 focus{10, 20};
    std::vector<glm::ivec2> sticky{{12, 20}};      // horiz=2 → near Relight
    std::vector<glm::ivec2> stale{{15, 20}};       // horiz=5 → far Remesh+Relight
    EnqueueStickyStaleRepairTickets(sched, focus, sticky, stale);
    Expect(sched.Contains(sticky[0], ColumnWorkKind::RemeshSeam),
           "sticky → RemeshSeam ticket");
    Expect(sched.Contains(sticky[0], ColumnWorkKind::RelightThenMesh),
           "near sticky → RelightThenMesh");
    Expect(sched.Contains(stale[0], ColumnWorkKind::RemeshSeam),
           "stale-dark → RemeshSeam");
    Expect(sched.Contains(stale[0], ColumnWorkKind::RelightThenMesh),
           "stale-dark → RelightThenMesh");
  }

  if (failures != 0)
  {
    std::cerr << failures << " failure(s)" << std::endl;
    return 1;
  }
  std::cout << "streaming_render_ready_invariants_test: PASS" << std::endl;
  return 0;
}
