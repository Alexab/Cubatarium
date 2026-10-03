#ifndef BLOCK_CRACK_OVERLAY_PASS_H
#define BLOCK_CRACK_OVERLAY_PASS_H

#include <array>
#include <glm/glm.hpp>
#include <memory>

typedef unsigned int GLuint;

namespace cutum
{

class UShaderManager;
class UShaderProgram;

/// Block being dug, as needed to place one crack overlay cube.
struct BlockCrackOverlayRequest
{
  glm::mat4 ViewProj{1.0f};
  glm::ivec3 BlockPos{0};
  float Progress{0.0f};
};

/// Draws destroy_stage_N.png over the block under the active break session.
class UBlockCrackOverlayPass
{
public:
  static constexpr int kStageCount = 10;

  bool InitShader(const std::shared_ptr<UShaderManager> &shader_manager);
  void DestroyGpuResources();

  /// Draws the crack cube. False means the caller should draw its fallback.
  bool Render(const BlockCrackOverlayRequest &request);

private:
  bool EnsureTextures();
  bool EnsureCube();

  std::shared_ptr<UShaderProgram> Shader;
  std::array<GLuint, kStageCount> StageTextures{};
  bool TexturesReady{false};
  bool LoadAttempted{false};

  GLuint Vao{0};
  GLuint Vbo{0};
  GLuint Ebo{0};
};

} // namespace cutum

#endif
