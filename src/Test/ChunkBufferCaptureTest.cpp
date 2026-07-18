#include "World/Chunks/ChunkBuffer.h"
#include "World/Core/BlockWorld.h"
#include "World/Math/FluidCellState.h"

#include <iostream>

namespace
{

constexpr const char *kTestName = "chunk_buffer_capture_test";

void Expect(bool ok, const char *msg)
{
  if (!ok)
  {
    std::cerr << kTestName << " FAIL: " << msg << std::endl;
    std::exit(1);
  }
}

} // namespace

int main()
{
  cutum::UBlockWorld world;
  cutum::UChunkBuffer buffer;
  world.SetCaptureBuffer(&buffer);

  const glm::ivec3 solid(1, 4, 2);
  const glm::ivec3 fluid_pos(1, 5, 2);
  world.SetBlock(solid, 8);
  world.SetBlock(fluid_pos, 9);
  const auto fluid =
      cutum::FluidCellState::Source().WithKind(cutum::FluidKind::Water);
  world.SetFluidState(fluid_pos, fluid);

  Expect(buffer.GetBlock(solid) == 8, "solid mirrored to capture buffer");
  Expect(buffer.GetBlock(fluid_pos) == 9, "fluid block mirrored");
  Expect(buffer.GetFluidPacked(fluid_pos) == cutum::PackFluidCellState(fluid),
         "fluid packed mirrored");

  world.SetBlock(solid, cutum::BLOCK_AIR);
  Expect(buffer.GetBlock(solid) == cutum::BLOCK_AIR, "AIR erase mirrored");

  world.SetCaptureBuffer(nullptr);
  world.SetBlock(glm::ivec3(3, 3, 3), 8);
  Expect(buffer.GetBlock(glm::ivec3(3, 3, 3)) == cutum::BLOCK_AIR,
         "no mirror after capture cleared");

  cutum::UBlockWorld live;
  buffer.SetBlock(glm::ivec3(0, 1, 0), 8);
  buffer.ApplyTo(live);
  Expect(live.GetBlock(glm::ivec3(0, 1, 0)) == 8, "ApplyTo still works");

  std::cout << kTestName << " OK" << std::endl;
  return 0;
}
