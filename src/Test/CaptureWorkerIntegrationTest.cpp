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
    std::cout << "capture_worker_integration_test: SKIP (disabled)\n";
    return 0;
  }
  cutum::UMeshCaptureWorker worker(1);
  cutum::UBlockWorld world;
  worker.Enqueue(world, glm::ivec3(0, 0, 0), 1, nullptr, nullptr);
  const auto deadline =
      std::chrono::steady_clock::now() + std::chrono::seconds(5);
  while (std::chrono::steady_clock::now() < deadline)
  {
    auto done = worker.DrainCompleted(4);
    if (!done.empty())
    {
      std::cout << "capture_worker_integration_test: PASS\n";
      return 0;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  std::cerr << "FAIL: worker capture timeout\n";
  return 1;
}
