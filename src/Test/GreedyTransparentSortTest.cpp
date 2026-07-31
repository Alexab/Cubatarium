#include "Render/Pipeline/GreedyTransparentSort.h"
#include "Render/Mesh/GreedyMeshBatch.h"
#include "Blocks/BlockDefinitionStorage.h"
#include "Blocks/BlockRegistry.h"

#include <cmath>
#include <iostream>
#include <memory>
#include <unordered_map>

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
  GreedyMeshBatch near_batch;
  near_batch.blockId = 1;
  near_batch.vertices.push_back({});
  near_batch.vertices.front().px = 0.0f;
  near_batch.vertices.front().py = 0.0f;
  near_batch.vertices.front().pz = 0.0f;
  near_batch.vertices.push_back({});
  near_batch.vertices.back().px = 1.0f;
  near_batch.vertices.back().py = 1.0f;
  near_batch.vertices.back().pz = 1.0f;

  GreedyMeshBatch far_batch = near_batch;
  far_batch.blockId = 2;
  far_batch.vertices.front().px = 100.0f;
  far_batch.vertices.front().py = 100.0f;
  far_batch.vertices.front().pz = 100.0f;
  far_batch.vertices.back().px = 101.0f;
  far_batch.vertices.back().py = 101.0f;
  far_batch.vertices.back().pz = 101.0f;

  const glm::vec3 cam(0.0f, 0.0f, 0.0f);
  const float near_d = GreedyBatchViewDistance(near_batch, cam);
  const float far_d = GreedyBatchViewDistance(far_batch, cam);
  Expect(far_d > near_d, "far batch has larger view distance");

  auto definitions = std::make_shared<UBlockDefinitionStorage>();
  UBlockRegistry registry(nullptr, definitions);
  std::vector<GreedyMeshBatch> batches{near_batch, far_batch};
  SortTransparentGreedyBatches(batches, cam, registry);
  Expect(batches[0].blockId == 2, "CPU sort draws far batch first");

  if (gFails != 0)
  {
    std::cerr << "greedy_transparent_sort_test: " << gFails << " failures\n";
    return 1;
  }
  std::cout << "greedy_transparent_sort_test: ok\n";
  return 0;
}
