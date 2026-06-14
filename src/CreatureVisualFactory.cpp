#include "CreatureVisual.h"
#include "CreatureVisualRigid.h"
#include "CreatureVisualGltf.h"
#include "CreatureDefinition.h"
#include <iostream>
#include <memory>

namespace cutum {

std::unique_ptr<ICreatureVisual> CreateCreatureVisual(const CreatureDefinition& def)
{
 if (def.visual.backend == "gltf_skeleton") {
  std::cout << "UCreatureVisual: gltf_skeleton stub for " << def.id << std::endl;
  return std::make_unique<UCreatureVisualGltf>();
 }
 return std::make_unique<UCreatureVisualRigid>();
}

} // namespace cutum
