#pragma once

#include "WorldGen/Core/WorldGenBlockResolver.h"
#include <functional>

namespace cutum
{

class UBlockWorld;
class UBlockRegistry;

struct ColumnWriteContext
{
  UBlockWorld &World;
  UBlockRegistry &Registry;
  WorldGenBlockResolver &Blocks;
  std::function<void(int world_x, int world_z, int min_y, int max_y)>
      OnColumnMeshDirty;
};

} // namespace cutum
