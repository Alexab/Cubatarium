#ifndef CREATUREBONEPALETTEGPU_H
#define CREATUREBONEPALETTEGPU_H

#include <glm/glm.hpp>
#include <array>
#include <cstddef>
#include <vector>

typedef unsigned int GLuint;

namespace cutum
{

/// 96: hermitcrab needs 67 joints; UBO 96*64B = 6KB, well under the
/// 16KB min-spec uniform block limit.
constexpr size_t kCreatureBonePaletteMaxBones = 96;
constexpr GLuint kCreatureBonePaletteBindingPoint = 0;

size_t ClampCreatureBonePaletteBoneCount(size_t boneCount);
std::array<glm::mat4, kCreatureBonePaletteMaxBones>
BuildCreatureBonePaletteData(const std::vector<glm::mat4> &boneMatrices);

void InitCreatureBonePaletteGpu();
void DestroyCreatureBonePaletteGpu();
size_t
UploadCreatureBonePaletteGpu(const std::vector<glm::mat4> &boneMatrices);
void BindCreatureBonePaletteBlock(GLuint programId, GLuint bindingPoint = 0);

} // namespace cutum

#endif
