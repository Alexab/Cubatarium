#ifndef CROSSINSTANCEBATCH_H
#define CROSSINSTANCEBATCH_H

#include "Blocks/BlockDefinition.h"
#include "World/Math/BlockTypes.h"
#include <array>
#include <cstddef>
#include <cstdint>
#include <glm/glm.hpp>
#include <memory>
#include <vector>

namespace cutum
{
class SectionRenderLightBinding;

struct CrossInstanceGpu
{
  glm::vec3 center{0.0f};
  float skyLight{0.0f};
  float blockLight{0.0f};
  std::array<uint32_t, 4> renderLightHandle{};
};

static_assert(sizeof(CrossInstanceGpu) == 36,
              "CrossInstanceGpu must match GPU layout");

/// Artifact ownership follows instance ranges through flattening and culling.
/// A transient ordered fallback handle must never be stored in these vertices.
struct CrossLightRange
{
  size_t first{0}, count{0};
  std::shared_ptr<SectionRenderLightBinding> binding;
};

struct CrossInstanceBatch
{
  BlockId blockId{BLOCK_AIR};
  BlockDecorShape shape{BlockDecorShape::Cross};
  std::vector<CrossInstanceGpu> instances;
  std::vector<CrossLightRange> lightRanges;
};

inline void
AppendCrossInstance(CrossInstanceBatch &batch, const CrossInstanceGpu &instance,
                    const std::shared_ptr<SectionRenderLightBinding> &binding)
{
  const auto index = batch.instances.size();
  batch.instances.push_back(instance);
  if (!batch.lightRanges.empty() &&
      batch.lightRanges.back().binding == binding &&
      batch.lightRanges.back().first + batch.lightRanges.back().count == index)
    ++batch.lightRanges.back().count;
  else
    batch.lightRanges.push_back({index, 1, binding});
}

template <class Predicate>
CrossInstanceBatch FilterCrossInstances(const CrossInstanceBatch &source,
                                        Predicate keep)
{
  CrossInstanceBatch result;
  result.blockId = source.blockId;
  result.shape = source.shape;
  result.instances.reserve(source.instances.size());
  result.lightRanges.reserve(source.lightRanges.size());
  size_t range = 0;
  for (size_t n = 0; n < source.instances.size(); ++n)
  {
    while (range < source.lightRanges.size() &&
           source.lightRanges[range].first + source.lightRanges[range].count <=
               n)
      ++range;
    if (!keep(source.instances[n]))
      continue;
    const auto binding = range < source.lightRanges.size() &&
                                 source.lightRanges[range].first <= n
                             ? source.lightRanges[range].binding
                             : nullptr;
    AppendCrossInstance(result, source.instances[n], binding);
  }
  return result;
}

} // namespace cutum

#endif
