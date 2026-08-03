#include "Render/Blocks/BlockCrackOverlayPass.h"

#include "App/Platform/GameDataRoot.h"
#include "App/Platform/IUPlatformPaths.h"
#include "Blocks/BlockDigRules.h"
#include "Render/Engine/ShaderManager.h"
#include "Render/GlIncludes.h"
#include "Render/Pipeline/GlStateScope.h"
#include "ThirdParty/stb_image.h"
#include "World/Math/GridMath.h"

#include <algorithm>
#include <cstddef>
#include <filesystem>
#include <glm/gtc/matrix_transform.hpp>
#include <iostream>
#include <optional>
#include <string>
#include <vector>

namespace cutum
{

namespace
{

namespace fs = std::filesystem;

constexpr GlStateMask kGlMaskCrackOverlay =
    GlStateBit::DepthTest | GlStateBit::DepthMask | GlStateBit::Blend |
    GlStateBit::CullFace;

/// Crack cube is grown slightly so it never z-fights the block faces.
constexpr float kHalfExtent = 0.5f + 0.004f;
constexpr size_t kFaceCount = 6;
constexpr size_t kVertexCount = kFaceCount * 4;
constexpr int kIndexCount = static_cast<int>(kFaceCount) * 6;
const glm::vec3 kFaceNormals[kFaceCount] = {
    {0.0f, 0.0f, -1.0f}, {0.0f, 0.0f, 1.0f},  {-1.0f, 0.0f, 0.0f},
    {1.0f, 0.0f, 0.0f},  {0.0f, -1.0f, 0.0f}, {0.0f, 1.0f, 0.0f},
};
const glm::vec3 kFaceTangents[kFaceCount] = {
    {1.0f, 0.0f, 0.0f}, {-1.0f, 0.0f, 0.0f}, {0.0f, 0.0f, -1.0f},
    {0.0f, 0.0f, 1.0f}, {1.0f, 0.0f, 0.0f},  {1.0f, 0.0f, 0.0f},
};
const glm::vec2 kFaceUv[4] = {
    {0.0f, 0.0f},
    {1.0f, 0.0f},
    {1.0f, 1.0f},
    {0.0f, 1.0f},
};

/// Pack that ships the destroy_stage sheet; other packs are scanned after it.
constexpr const char *kPreferredPack = "cubatarium_cc0_base";

std::vector<fs::path> AssetSearchRoots()
{
  std::vector<fs::path> roots;
  const auto add_root = [&roots](const fs::path &candidate)
  {
    if (candidate.empty())
    {
      return;
    }
    if (std::find(roots.begin(), roots.end(), candidate) != roots.end())
    {
      return;
    }
    roots.push_back(candidate);
  };

  if (auto *paths = IUPlatformPaths::TryGet())
  {
    add_root(paths->AssetRoot());
  }
  std::error_code ec;
  const fs::path cwd = fs::current_path(ec);
  if (!ec)
  {
    if (auto from_cwd = TryFindProjectRoot(cwd))
    {
      add_root(*from_cwd);
    }
    add_root(cwd);
  }
  return roots;
}

/// Directory holding destroy_stage_N.png, searched from asset root then cwd.
std::optional<fs::path> FindStageTextureDir()
{
  const fs::path stage0("destroy_stage_0.png");
  std::error_code ec;
  for (const fs::path &root : AssetSearchRoots())
  {
    const fs::path packs = root / "resource_packs";
    if (!fs::exists(packs, ec))
    {
      continue;
    }
    const fs::path preferred = packs / kPreferredPack / "textures" / "ui";
    if (fs::exists(preferred / stage0, ec))
    {
      return preferred;
    }
    for (const auto &entry : fs::directory_iterator(packs, ec))
    {
      if (!entry.is_directory())
      {
        continue;
      }
      const fs::path ui_dir = entry.path() / "textures" / "ui";
      if (fs::exists(ui_dir / stage0, ec))
      {
        return ui_dir;
      }
    }
  }
  return std::nullopt;
}

GLuint LoadStageTexture(const fs::path &image_path)
{
  int width = 0;
  int height = 0;
  int channels = 0;
  unsigned char *data =
      stbi_load(image_path.string().c_str(), &width, &height, &channels, 4);
  if (!data)
  {
    return 0;
  }

  GLuint texture_id = 0;
  glGenTextures(1, &texture_id);
  if (texture_id == 0)
  {
    stbi_image_free(data);
    return 0;
  }
  glBindTexture(GL_TEXTURE_2D, texture_id);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA,
               GL_UNSIGNED_BYTE, data);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glBindTexture(GL_TEXTURE_2D, 0);
  stbi_image_free(data);
  return texture_id;
}

} // namespace

bool UBlockCrackOverlayPass::InitShader(
    const std::shared_ptr<UShaderManager> &shader_manager)
{
  if (!shader_manager)
  {
    return false;
  }
  Shader = shader_manager->CreateShader("block_crack",
                                        "shaders/vshader_block_crack.glsl",
                                        "shaders/fshader_block_crack.glsl");
  if (!Shader || !Shader->IsValid())
  {
    std::cerr << "Failed to create block crack shader" << std::endl;
    return false;
  }
  return true;
}

void UBlockCrackOverlayPass::DestroyGpuResources()
{
  for (GLuint &texture : StageTextures)
  {
    if (texture != 0)
    {
      glDeleteTextures(1, &texture);
      texture = 0;
    }
  }
  TexturesReady = false;
  LoadAttempted = false;

  if (Ebo != 0)
  {
    glDeleteBuffers(1, &Ebo);
    Ebo = 0;
  }
  if (Vbo != 0)
  {
    glDeleteBuffers(1, &Vbo);
    Vbo = 0;
  }
  if (Vao != 0)
  {
    glDeleteVertexArrays(1, &Vao);
    Vao = 0;
  }
}

bool UBlockCrackOverlayPass::EnsureTextures()
{
  if (TexturesReady)
  {
    return true;
  }
  if (LoadAttempted)
  {
    return false;
  }
  LoadAttempted = true;

  const std::optional<fs::path> dir = FindStageTextureDir();
  if (!dir)
  {
    std::cerr << "Block crack textures not found (destroy_stage_0.png); "
              << "using wireframe crack overlay" << std::endl;
    return false;
  }

  bool all_loaded = true;
  for (int stage = 0; stage < kStageCount; ++stage)
  {
    const fs::path file =
        *dir / ("destroy_stage_" + std::to_string(stage) + ".png");
    const GLuint texture = LoadStageTexture(file);
    StageTextures[static_cast<size_t>(stage)] = texture;
    if (texture == 0)
    {
      all_loaded = false;
    }
  }

  if (!all_loaded)
  {
    for (GLuint &texture : StageTextures)
    {
      if (texture != 0)
      {
        glDeleteTextures(1, &texture);
        texture = 0;
      }
    }
    std::cerr << "Incomplete destroy_stage set in " << dir->string()
              << "; using wireframe crack overlay" << std::endl;
    return false;
  }

  TexturesReady = true;
  return true;
}

bool UBlockCrackOverlayPass::EnsureCube()
{
  if (Vao != 0)
  {
    return true;
  }

  float vertices[kVertexCount * 5] = {0.0f};
  unsigned int indices[kIndexCount] = {0};
  for (size_t face = 0; face < kFaceCount; ++face)
  {
    const glm::vec3 normal = kFaceNormals[face];
    const glm::vec3 tangent = kFaceTangents[face];
    const glm::vec3 bitangent = glm::cross(tangent, normal);
    for (size_t corner = 0; corner < 4; ++corner)
    {
      const glm::vec2 uv = kFaceUv[corner];
      const glm::vec3 pos =
          kHalfExtent * (normal + tangent * (2.0f * uv.x - 1.0f) +
                         bitangent * (2.0f * uv.y - 1.0f));
      float *out = &vertices[(face * 4 + corner) * 5];
      out[0] = pos.x;
      out[1] = pos.y;
      out[2] = pos.z;
      out[3] = uv.x;
      // PNG rows run top-down while the face V axis runs bottom-up.
      out[4] = 1.0f - uv.y;
    }
    const unsigned int base = static_cast<unsigned int>(face) * 4u;
    const size_t out = face * 6;
    indices[out + 0] = base + 0u;
    indices[out + 1] = base + 2u;
    indices[out + 2] = base + 1u;
    indices[out + 3] = base + 0u;
    indices[out + 4] = base + 3u;
    indices[out + 5] = base + 2u;
  }

  glGenVertexArrays(1, &Vao);
  glGenBuffers(1, &Vbo);
  glGenBuffers(1, &Ebo);
  if (Vao == 0 || Vbo == 0 || Ebo == 0)
  {
    return false;
  }

  glBindVertexArray(Vao);
  glBindBuffer(GL_ARRAY_BUFFER, Vbo);
  glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);
  constexpr GLsizei k_stride = 5 * static_cast<GLsizei>(sizeof(float));
  glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, k_stride, nullptr);
  glEnableVertexAttribArray(0);
  glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, k_stride,
                        reinterpret_cast<void *>(3 * sizeof(float)));
  glEnableVertexAttribArray(1);
  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, Ebo);
  glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(indices), indices,
               GL_STATIC_DRAW);
  glBindVertexArray(0);
  return true;
}

bool UBlockCrackOverlayPass::Render(const BlockCrackOverlayRequest &request)
{
  if (!(request.Progress > 0.0f) || !Shader || !Shader->IsValid())
  {
    return false;
  }
  if (!EnsureTextures() || !EnsureCube())
  {
    return false;
  }

  const int stage =
      BlockDigRules::CrackStageIndex(request.Progress, kStageCount);
  const GLuint texture = StageTextures[static_cast<size_t>(stage)];
  if (texture == 0)
  {
    return false;
  }

  UGlStateScope gl_guard(kGlMaskCrackOverlay);
  glEnable(GL_DEPTH_TEST);
  glDepthMask(GL_FALSE);
  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  glDisable(GL_CULL_FACE);
  glEnable(GL_POLYGON_OFFSET_FILL);
  glPolygonOffset(-1.0f, -1.0f);

  const glm::mat4 mvp =
      request.ViewProj *
      glm::translate(glm::mat4(1.0f), BlockCenter(request.BlockPos));
  Shader->Use();
  Shader->SetMat4("uMvp", mvp);
  Shader->SetInt("uCrackTex", 0);
  Shader->SetFloat("uAlpha",
                   0.6f + 0.35f * std::clamp(request.Progress, 0.0f, 1.0f));
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, texture);
  glBindVertexArray(Vao);
  glDrawElements(GL_TRIANGLES, kIndexCount, GL_UNSIGNED_INT, nullptr);
  glBindVertexArray(0);
  glBindTexture(GL_TEXTURE_2D, 0);
  Shader->Unuse();

  glPolygonOffset(0.0f, 0.0f);
  glDisable(GL_POLYGON_OFFSET_FILL);
  return true;
}

} // namespace cutum
