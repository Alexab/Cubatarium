#ifndef CREATUREBONEPALETTEGPU_H
#define CREATUREBONEPALETTEGPU_H

#include <glm/glm.hpp>
#include <array>
#include <cstddef>
#include <vector>

typedef unsigned int GLuint;

namespace cutum
{

constexpr size_t kCreatureBonePaletteMaxBones = 64;
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
