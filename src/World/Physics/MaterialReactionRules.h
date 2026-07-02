#ifndef MATERIALREACTIONRULES_H
#define MATERIALREACTIONRULES_H

#include "World/Math/BlockTypes.h"
#include <glm/glm.hpp>
#include <vector>

namespace cutum
{

class UBlockRegistry;
class UBlockWorld;

enum class MaterialReactionKind
{
  WaterMeetsLava,
  FireSpread
};

struct MaterialReactionResult
{
  bool Applied{false};
  glm::ivec3 BlockPos{0};
  BlockId NewBlock{BLOCK_AIR};
};

class UMaterialReactionRules
{
public:
  bool ShadowMode{true};
  std::vector<MaterialReactionResult> EvaluateNeighbors(UBlockWorld &block_world,
                                                        const UBlockRegistry &registry,
                                                        glm::ivec3 changed_pos) const;
};

} // namespace cutum

#endif // MATERIALREACTIONRULES_H
