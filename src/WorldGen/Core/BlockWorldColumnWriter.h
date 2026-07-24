#pragma once

#include "WorldGen/Core/IUColumnWriter.h"

namespace cutum
{

class UBlockWorldColumnWriter : public IUColumnWriter
{
public:
  UBlockWorldColumnWriter(UBlockWorld &world, UBlockRegistry &registry);

  UBlockWorld &GetBlockWorld() override { return World; }
  UBlockRegistry &GetRegistry() override { return Registry; }
  void OnColumnFinished(int world_x, int world_z, int min_y,
                        int max_y) override;

private:
  UBlockWorld &World;
  UBlockRegistry &Registry;
};

} // namespace cutum
