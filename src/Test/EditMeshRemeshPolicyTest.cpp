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
    Expect(d.ImmediateChunks.size() == 7,
           "gpu patch: center + 6 face neighbors immediate");
    Expect(!d.DirtyChunks.empty(), "gpu patch: light ring dirty");
  }

  {
    // Face neighbors Immediate under PreferGpuStorePatch (seam fix).
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
    Expect(d.ImmediateChunks.size() == 7,
           "gpu patch without light ring: center + 6 face neighbors");
  }

  {
    // Real seam neighbors precede unrelated face neighbors at a chunk edge.
    EditMeshRemeshInput in;
    in.BlockPositions = {{16, 66, 8}};
    in.SyncNeighborChunks = true;
    in.AsyncMeshing = true;
    in.GreedyMeshing = true;
    in.HasRegistry = true;
    in.ImmediateChunkCap = 2;
    in.PreferGpuStorePatch = true;
    const auto d = EvaluateEditMeshRemesh(in);
    Expect(d.ImmediateChunks.size() == 2, "boundary edit: center + seam");
    Expect(d.ImmediateChunks.size() > 1 &&
               d.ImmediateChunks[1] == glm::ivec3(0, 4, 0),
           "boundary edit: -X seam neighbor prioritized");
  }

  {
    // An edge edit admits the center and both actually affected seam slices.
    EditMeshRemeshInput in;
    in.BlockPositions = {{16, 64, 8}};
    in.SyncNeighborChunks = true;
    in.AsyncMeshing = true;
    in.GreedyMeshing = true;
    in.HasRegistry = true;
    in.ImmediateChunkCap = 3;
    in.PreferGpuStorePatch = true;
    const auto d = EvaluateEditMeshRemesh(in);
    Expect(d.ImmediateChunks.size() == 3, "edge edit: center + two seams");
    Expect(d.ImmediateChunks.size() > 2 &&
               d.ImmediateChunks[1] == glm::ivec3(0, 4, 0) &&
               d.ImmediateChunks[2] == glm::ivec3(1, 3, 0),
           "edge edit: both seam neighbors prioritized");
  }

  if (gFails != 0)
  {
    std::cerr << "edit_mesh_remesh_policy_test: " << gFails << " failures\n";
    return 1;
  }
  std::cout << "edit_mesh_remesh_policy_test: ok\n";
  return 0;
}
