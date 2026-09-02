#include "Render/Mesh/MeshCaptureToken.h"
#include "Render/Mesh/MeshCaptureWorker.h"
#include "World/Core/BlockWorld.h"

#include <chrono>
#include <glm/glm.hpp>
#include <iostream>
#include <thread>

int main()
{
  using cutum::UMeshCaptureWorker;
  if (!UMeshCaptureWorker::kWorkerCaptureEnabled)
  {
    std::cout << "mesh_schedule_retry_test: SKIP (disabled)\n";
    return 0;
  }
  cutum::UMeshCaptureWorker worker(1);
  cutum::UBlockWorld world;
  world.GetChunkManager().EnsureChunk(glm::ivec3(0, 0, 0));
  const cutum::MeshCaptureToken token{1, 1, 1};
  auto band = world.ReadChunkBandForCapture(glm::ivec3(0, 0, 0), token);
  if (!band)
  {
    std::cerr << "mesh_schedule_retry_test: FAIL ReadChunkBandForCapture\n";
    return 1;
  }
  worker.Enqueue(std::move(*band), glm::ivec3(0, 0, 0), 1);
  const auto deadline =
      std::chrono::steady_clock::now() + std::chrono::seconds(5);
  while (std::chrono::steady_clock::now() < deadline)
  {
    auto done = worker.DrainCompleted(4);
    if (!done.empty())
    {
      std::cout << "mesh_schedule_retry_test: PASS\n";
      return 0;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  std::cerr << "mesh_schedule_retry_test: FAIL worker timeout\n";
  return 1;
}
