#include "Creatures/Visual/CreatureBonePaletteGpu.h"

#include "Render/GlIncludes.h"
#include <algorithm>

namespace cutum
{

namespace
{

GLuint gCreatureBoneUbo{0};

} // namespace

size_t ClampCreatureBonePaletteBoneCount(size_t boneCount)
{
  return std::min(boneCount, kCreatureBonePaletteMaxBones);
}

std::array<glm::mat4, kCreatureBonePaletteMaxBones>
BuildCreatureBonePaletteData(const std::vector<glm::mat4> &boneMatrices)
{
  std::array<glm::mat4, kCreatureBonePaletteMaxBones> palette{};
  const size_t copyCount = ClampCreatureBonePaletteBoneCount(boneMatrices.size());
  for (size_t i = 0; i < copyCount; ++i)
  {
    palette[i] = boneMatrices[i];
  }
  for (size_t i = copyCount; i < palette.size(); ++i)
  {
    palette[i] = glm::mat4(1.f);
  }
  return palette;
}

void InitCreatureBonePaletteGpu()
{
  if (gCreatureBoneUbo != 0)
  {
    return;
  }
  glGenBuffers(1, &gCreatureBoneUbo);
  glBindBuffer(GL_UNIFORM_BUFFER, gCreatureBoneUbo);
  glBufferData(GL_UNIFORM_BUFFER,
               kCreatureBonePaletteMaxBones * sizeof(glm::mat4), nullptr,
               GL_DYNAMIC_DRAW);
  glBindBuffer(GL_UNIFORM_BUFFER, 0);
}

void DestroyCreatureBonePaletteGpu()
{
  if (gCreatureBoneUbo != 0)
  {
    glDeleteBuffers(1, &gCreatureBoneUbo);
    gCreatureBoneUbo = 0;
  }
}

size_t
UploadCreatureBonePaletteGpu(const std::vector<glm::mat4> &boneMatrices)
{
  if (gCreatureBoneUbo == 0)
  {
    InitCreatureBonePaletteGpu();
  }
  const auto palette = BuildCreatureBonePaletteData(boneMatrices);
  const size_t boneCount = ClampCreatureBonePaletteBoneCount(boneMatrices.size());
  glBindBuffer(GL_UNIFORM_BUFFER, gCreatureBoneUbo);
  glBufferSubData(GL_UNIFORM_BUFFER, 0, sizeof(palette), palette.data());
  glBindBufferBase(GL_UNIFORM_BUFFER, kCreatureBonePaletteBindingPoint,
                   gCreatureBoneUbo);
  glBindBuffer(GL_UNIFORM_BUFFER, 0);
  return boneCount;
}

void BindCreatureBonePaletteBlock(GLuint programId, GLuint bindingPoint)
{
  const GLuint blockIndex = glGetUniformBlockIndex(programId, "BonePalette");
  if (blockIndex != GL_INVALID_INDEX)
  {
    glUniformBlockBinding(programId, blockIndex, bindingPoint);
  }
}

} // namespace cutum
