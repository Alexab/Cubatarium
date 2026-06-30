#ifndef CREATUREBONEPALETTEGPU_H
#define CREATUREBONEPALETTEGPU_H

#include <glm/glm.hpp>
#include <vector>

typedef unsigned int GLuint;

namespace cutum
{

void InitCreatureBonePaletteGpu();
void DestroyCreatureBonePaletteGpu();
void UploadCreatureBonePaletteGpu(const std::vector<glm::mat4> &boneMatrices);
void BindCreatureBonePaletteBlock(GLuint programId, GLuint bindingPoint = 0);

} // namespace cutum

#endif
