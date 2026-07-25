#include "World/Mesh/EditMeshRemeshPolicy.h"

#include <cstdlib>
#include <iostream>
#include <string>

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
  using cutum::EditMeshRemeshInput;
  using cutum::EvaluateEditMeshRemesh;

  {
    EditMeshRemeshInput in;
    in.BlockPositions = {{8, 64, 8}};
    in.SyncNeighborChunks = true;
    in.SyncLightRing = false;
    in.AsyncMeshing = true;
    in.GreedyMeshing = true;
    in.HasRegistry = true;
    in.ImmediateChunkCap = 9;
    const auto d = EvaluateEditMeshRemesh(in);
    Expect(!d.ImmediateChunks.empty(), "edit has immediate chunks");
    Expect(static_cast<int>(d.ImmediateChunks.size()) <= 9,
           "immediate within cap");
  }

  {
    EditMeshRemeshInput in;
    in.BlockPositions = {{8, 64, 8}};
    in.SyncNeighborChunks = true;
    in.SyncLightRing = true;
    in.AsyncMeshing = true;
    in.GreedyMeshing = true;
    in.HasRegistry = true;
    in.ImmediateChunkCap = 9;
    const auto d = EvaluateEditMeshRemesh(in);
    Expect(static_cast<int>(d.ImmediateChunks.size()) == 9,
           "immediate exactly capped at 9 with light ring");
    Expect(!d.DirtyChunks.empty(), "overflow goes dirty");
  }

  {
    EditMeshRemeshInput in;
    in.BlockPositions = {{8, 64, 8}};
    in.HasRegistry = false;
    in.AsyncMeshing = true;
    in.GreedyMeshing = true;
    const auto d = EvaluateEditMeshRemesh(in);
    Expect(d.ImmediateChunks.empty(), "no registry => no immediate");
    Expect(!d.DirtyChunks.empty(), "no registry => dirty");
  }

  {
    EditMeshRemeshInput in;
    in.BlockPositions = {{8, 64, 8}};
    in.SyncNeighborChunks = true;
    in.SyncLightRing = true;
    in.AsyncMeshing = true;
    in.GreedyMeshing = true;
    in.HasRegistry = true;
    in.ImmediateChunkCap = 9;
    in.PreferGpuStorePatch = true;
    const auto d = EvaluateEditMeshRemesh(in);
    Expect(d.ImmediateChunks.size() == 1, "gpu patch: only center immediate");
    Expect(!d.DirtyChunks.empty(), "gpu patch: ring dirty");
  }

  {
    // Face neighbors without light ring still Immediate under default policy;
    // with PreferGpuStorePatch only center.
    EditMeshRemeshInput in;
    in.BlockPositions = {{8, 64, 8}};
    in.SyncNeighborChunks = true;
    in.SyncLightRing = false;
    in.AsyncMeshing = true;
    in.GreedyMeshing = true;
    in.HasRegistry = true;
    in.ImmediateChunkCap = 9;
    in.PreferGpuStorePatch = true;
    const auto d = EvaluateEditMeshRemesh(in);
    Expect(d.ImmediateChunks.size() == 1,
           "gpu patch without light ring: center only");
  }

  if (gFails != 0)
  {
    std::cerr << "edit_mesh_remesh_policy_test: " << gFails << " failures\n";
    return 1;
  }
  std::cout << "edit_mesh_remesh_policy_test: ok\n";
  return 0;
}
