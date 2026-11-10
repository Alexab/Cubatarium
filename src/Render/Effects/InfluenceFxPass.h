#ifndef INFLUENCE_FX_PASS_H
#define INFLUENCE_FX_PASS_H

#include <glm/glm.hpp>
#include <memory>

typedef unsigned int GLuint;

namespace cutum
{

class UShaderManager;
class UShaderProgram;

class UInfluenceFxPass
{
public:
  bool InitShaders(const std::shared_ptr<UShaderManager> &shader_manager);
  void DestroyGpuResources();
  void UpdateAndRender(const glm::mat4 &view_proj, float dt);

private:
  bool EnsureLineBuffers();

  std::shared_ptr<UShaderProgram> LineShader;
  GLuint LineVao{0};
  GLuint LineVbo{0};
};

} // namespace cutum

#endif
