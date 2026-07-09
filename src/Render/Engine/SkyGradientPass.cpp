#include "Render/Engine/SkyGradientPass.h"

#include "Render/Engine/ShaderManager.h"
#include "Render/Engine/UnderwaterFogPass.h"
#include "Render/GlIncludes.h"

#include <algorithm>
#include <iostream>

namespace cutum
{

void USkyGradientPass::Draw(const std::shared_ptr<UShaderProgram> &sky_shader,
                            const glm::vec4 &sky_color,
                            const UUnderwaterFogPass &fog_pass,
                            float horizon_boost)
{
  if (!sky_shader || !sky_shader->IsValid())
  {
    std::cerr << "Sky shader is not linked!" << std::endl;
    return;
  }

  glDisable(GL_DEPTH_TEST);

  sky_shader->Use();

  const glm::mat4 sky_matrix = glm::mat4(1.0f);
  sky_shader->SetMat4("mvp_matrix", sky_matrix);
  sky_shader->SetVec4("skyColor", sky_color);
  sky_shader->SetVec3("uFogColor", fog_pass.GetFogColor());
  sky_shader->SetFloat(
      "uFogHorizonBlend",
      std::clamp(fog_pass.GetFogHorizonBlend() + horizon_boost, 0.0f, 1.0f));

  static const GLfloat sky_vertices[] = {
      -1.0f, -1.0f, 0.0f, 0.0f, 0.0f, 1.0f,  -1.0f, 0.0f, 1.0f, 0.0f,
      1.0f,  1.0f,  0.0f, 1.0f, 1.0f, -1.0f, 1.0f,  0.0f, 0.0f, 1.0f};

  GLuint temp_vbo = 0;
  glGenBuffers(1, &temp_vbo);
  glBindBuffer(GL_ARRAY_BUFFER, temp_vbo);
  glBufferData(GL_ARRAY_BUFFER, sizeof(sky_vertices), sky_vertices,
               GL_STATIC_DRAW);

  const int vertex_location =
      glGetAttribLocation(sky_shader->GetProgramID(), "a_position");
  if (vertex_location != -1)
  {
    glEnableVertexAttribArray(vertex_location);
    glVertexAttribPointer(vertex_location, 3, GL_FLOAT, GL_FALSE,
                          5 * sizeof(GLfloat), (void *)0);
  }

  const int texcoord_location =
      glGetAttribLocation(sky_shader->GetProgramID(), "a_texcoord");
  if (texcoord_location != -1)
  {
    glEnableVertexAttribArray(texcoord_location);
    glVertexAttribPointer(texcoord_location, 2, GL_FLOAT, GL_FALSE,
                          5 * sizeof(GLfloat), (void *)(3 * sizeof(GLfloat)));
  }

  glDrawArrays(GL_TRIANGLE_FAN, 0, 4);

  const GLenum error = glGetError();
  if (error != GL_NO_ERROR)
  {
    std::cerr << "OpenGL error after drawing sky: " << error << std::endl;
  }

  glBindBuffer(GL_ARRAY_BUFFER, 0);
  glDeleteBuffers(1, &temp_vbo);
  sky_shader->Unuse();

  glEnable(GL_DEPTH_TEST);
}

} // namespace cutum
