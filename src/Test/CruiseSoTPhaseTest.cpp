#include "World/Chunks/BlockQuery.h"
#include "World/Streaming/ColumnRecord.h"
#include "World/Streaming/ColumnTicketMap.h"
#include "World/Streaming/WorldBorderPolicy.h"

#include <cstdlib>
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

int main()
{
  using namespace cutum;

  // --- BlockQuery ---
  Expect(MakeUnloadedQuery().IsUnloaded(), "unloaded query");
  Expect(!MakeUnloadedQuery().IsAir(), "unloaded is not air");
  Expect(MakeAirQuery().IsAir(), "air query");
  Expect(MakeSolidQuery(BLOCK_AIR).IsAir(), "solid AIR folds to air");
  Expect(MakeSolidQuery(static_cast<BlockId>(1)).IsSolid(), "solid id");

  // --- WorldBorder ---
  WorldBorderConfig cfg;
  cfg.soft_half_extent = 1000.0f;
  cfg.soft_margin = 100.0f;
  cfg.hard_half_extent = 100000.0f;
  Expect(IsInsideSoftWorldBorder(glm::vec3(0, 0, 0), cfg), "origin inside soft");
  Expect(!IsInsideSoftWorldBorder(glm::vec3(2000, 0, 0), cfg), "far outside soft");
  glm::vec3 p(1500.0f, 64.0f, 0.0f);
  Expect(ClampToSoftWorldBorder(p, cfg), "clamp fires");
  Expect(p.x == 1000.0f, "clamped to soft extent");
  Expect(SoftBorderSpeedScale(glm::vec3(0, 0, 0), cfg) == 1.0f, "full speed center");
  Expect(SoftBorderSpeedScale(glm::vec3(1000, 0, 0), cfg) <= 0.26f,
         "edge speed slow");

  // --- Tickets / pools ---
  Expect(TicketLevelForRing(0) == ColumnTicketLevel::MeshFull, "ring0 full");
  Expect(TicketLevelForRing(2) == ColumnTicketLevel::MeshFull, "ring2 full");
  Expect(TicketLevelForRing(3) == ColumnTicketLevel::MeshLit, "ring3 lit");
  Expect(TicketLevelForRing(5) == ColumnTicketLevel::MeshDeferred, "ring5 deferred");
  WorkPoolBudget base = DefaultCruisePools();
  WorkPoolBudget hole = HoleDrainPools(base);
  Expect(hole.remesh_slots == 1, "hole drain keeps 1 remesh reservation");
  Expect(hole.first_mesh_slots == base.first_mesh_slots + base.remesh_slots - 1,
         "hole drain FM gets stolen remesh slots");

  // --- ColumnRecord store ---
  UColumnRecordStore store;
  store.SetEmerge(glm::ivec2(1, 2), ColumnEmergeState::Meshing);
  store.SetDesired(glm::ivec2(1, 2), ColumnDesiredStage::FirstMesh);
  const ColumnRecord *rec = store.Find(glm::ivec2(1, 2));
  Expect(rec != nullptr, "record exists");
  Expect(rec->emerge == ColumnEmergeState::Meshing, "emerge mirrored");
  Expect(rec->desired == ColumnDesiredStage::FirstMesh, "desired mirrored");
  store.Erase(glm::ivec2(1, 2));
  Expect(store.Find(glm::ivec2(1, 2)) == nullptr, "erase clears");

  if (gFails != 0)
  {
    std::cerr << gFails << " failures\n";
    return 1;
  }
  std::cout << "CruiseSoTPhaseTest OK\n";
  return 0;
}
