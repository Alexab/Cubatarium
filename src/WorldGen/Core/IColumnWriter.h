#pragma once

#include "World/Math/BlockTypes.h"

namespace cutum
{

class UBlockRegistry;
class UBlockWorld;

class IColumnWriter
{
public:
  virtual ~IColumnWriter() = default;
  virtual UBlockWorld &GetBlockWorld() = 0;
  virtual UBlockRegistry &GetRegistry() = 0;
  virtual void OnColumnFinished(int world_x, int world_z, int min_y,
                                int max_y) = 0;
};

} // namespace cutum
