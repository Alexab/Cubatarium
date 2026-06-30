#include "Creatures/Visual/CreatureBonePaletteGpu.h"

#include "Render/GlIncludes.h"

namespace cutum
{

namespace
{

GLuint gCreatureBoneUbo{0};

} // namespace

void InitCreatureBonePaletteGpu()
{
  if (gCreatureBoneUbo != 0)
  {
    return;
  }
  glGenBuffers(1, &gCreatureBoneUbo);
  glBindBuffer(GL_UNIFORM_BUFFER, gCreatureBoneUbo);
  glBufferData(GL_UNIFORM_BUFFER, 64 * sizeof(glm::mat4), nullptr,
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

void UploadCreatureBonePaletteGpu(const std::vector<glm::mat4> &boneMatrices)
{
  if (gCreatureBoneUbo == 0)
  {
    InitCreatureBonePaletteGpu();
  }
  glm::mat4 palette[64];
  for (int i = 0; i < 64; ++i)
  {
    palette[i] = (static_cast<size_t>(i) < boneMatrices.size())
                     ? boneMatrices[static_cast<size_t>(i)]
                     : glm::mat4(1.f);
  }
  glBindBuffer(GL_UNIFORM_BUFFER, gCreatureBoneUbo);
  glBufferSubData(GL_UNIFORM_BUFFER, 0, sizeof(palette), palette);
  glBindBufferBase(GL_UNIFORM_BUFFER, 0, gCreatureBoneUbo);
  glBindBuffer(GL_UNIFORM_BUFFER, 0);
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
