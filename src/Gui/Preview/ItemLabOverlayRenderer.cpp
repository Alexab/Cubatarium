#include "Gui/Preview/ItemLabOverlayRenderer.h"
#include "Items/ItemGltfTextureCache.h"
#include "Render/Engine/ShaderManager.h"
#include "Render/Engine/TerrainGpuBuffers.h"
#include "Render/GlIncludes.h"
#include "Render/Pipeline/GlStateScope.h"
#include <cmath>
#include <cstddef>
#include <glm/gtc/matrix_transform.hpp>
#include <limits>
#include <set>
#include <stdexcept>

namespace cutum
{
UItemLabOverlayRenderer::UItemLabOverlayRenderer(UShaderManager &shaders)
{
  Shader =
      shaders.CreateShader("item_lab_overlay", "shaders/item_lab_overlay.vert",
                           "shaders/item_lab_overlay.frag");
  if (!Shader || !Shader->IsValid())
    throw std::runtime_error("Cannot initialize Item Lab diagnostics shader");
  UGlStateScope scope(kGlMaskIconFbo);
  GLint previous = 0;
  glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &previous);
  glGenVertexArrays(1, &Vao);
  glGenBuffers(1, &Vbo);
  if (!Vao || !Vbo)
  {
    glDeleteVertexArrays(1, &Vao);
    glDeleteBuffers(1, &Vbo);
    throw std::runtime_error("Cannot allocate Item Lab diagnostics buffers");
  }
  glBindVertexArray(Vao);
  glBindBuffer(GL_ARRAY_BUFFER, Vbo);
  const size_t offsets[] = {offsetof(Vertex, Position), offsetof(Vertex, Color),
                            offsetof(Vertex, Uv)};
  const int sizes[] = {3, 4, 2};
  for (unsigned i = 0; i < 3; ++i)
  {
    glVertexAttribPointer(i, sizes[i], GL_FLOAT, GL_FALSE, sizeof(Vertex),
                          reinterpret_cast<void *>(offsets[i]));
    glEnableVertexAttribArray(i);
  }
  glBindBuffer(GL_ARRAY_BUFFER, unsigned(previous));
}
UItemLabOverlayRenderer::~UItemLabOverlayRenderer()
{
  glDeleteVertexArrays(1, &Vao);
  glDeleteBuffers(1, &Vbo);
}
void UItemLabOverlayRenderer::InspectGpuBuffers(TerrainGpuBufferCensus &out) const
{
  out.Add(TerrainGpuBufferRole::PreviewOverlay, Vbo);
}
bool UItemLabOverlayRenderer::Draw(const std::vector<Vertex> &vertices,
                                   unsigned mode, const glm::mat4 &matrix,
                                   unsigned texture)
{
  if (vertices.empty())
    return true;
  if (vertices.size() > static_cast<size_t>(std::numeric_limits<GLsizei>::max()) ||
      vertices.size() > static_cast<size_t>(std::numeric_limits<GLsizeiptr>::max()) / sizeof(Vertex))
    return false;
  GLint previous = 0;
  glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &previous);
  glDisable(GL_DEPTH_TEST);
  glDisable(GL_BLEND);
  glDisable(GL_CULL_FACE);
  Shader->Use();
  Shader->SetMat4("uMvp", matrix);
  Shader->SetInt("uAlbedo", 0);
  Shader->SetInt("uTextured", texture != 0);
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, texture);
  glBindVertexArray(Vao);
  glBindBuffer(GL_ARRAY_BUFFER, Vbo);
  glBufferData(GL_ARRAY_BUFFER, GLsizeiptr(vertices.size() * sizeof(Vertex)),
               vertices.data(), GL_STREAM_DRAW);
  const bool uploaded = glGetError() == GL_NO_ERROR;
  if (uploaded)
    glDrawArrays(mode, 0, GLsizei(vertices.size()));
  glBindBuffer(GL_ARRAY_BUFFER, unsigned(previous));
  return uploaded;
}
bool UItemLabOverlayRenderer::Lines(const std::vector<ItemLabLineVertex> &lines,
                                    const glm::mat4 &matrix)
{
  std::vector<Vertex> vertices;
  vertices.reserve(lines.size());
  for (const auto &line : lines)
    vertices.push_back({line.Position, line.Color, {0, 0}});
  return Draw(vertices, GL_LINES, matrix);
}
size_t UItemLabOverlayRenderer::DrawContacts(const ItemRenderAsset &asset,
                                             const ItemVisualState &state,
                                             glm::vec3 physicalSize,
                                             const glm::mat4 &mvp)
{
  UGlStateScope scope(kGlMaskIconFbo);
  const auto lines = ItemLabContactLines(asset, state, physicalSize);
  return Lines(lines, mvp) ? lines.size() : 0;
}
size_t UItemLabOverlayRenderer::DrawUvTiles(const ItemRenderAsset &asset)
{
  UGlStateScope scope(kGlMaskIconFbo);
  std::set<std::filesystem::path> images;
  for (const auto &primitive : asset.Primitives)
    images.insert(
        asset.Materials.at(primitive.Material).Albedo.lexically_normal());
  if (images.empty())
    throw std::runtime_error("Item Lab UV view has no referenced images");
  const int columns = int(std::ceil(std::sqrt(double(images.size()))));
  const int rows = (int(images.size()) + columns - 1) / columns;
  const float pad = .025f, width = 1.f / columns, height = 1.f / rows;
  const auto projection = glm::ortho(0.f, 1.f, 0.f, 1.f, -1.f, 1.f);
  size_t lineVertices = 0;
  int tile = 0;
  for (const auto &path : images)
  {
    const auto texture = ItemGltfTextureCache::Instance().GetLinearAlbedo(path);
    if (!texture)
      throw std::runtime_error("Item Lab UV texture unavailable: " +
                               path.string());
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, texture);
    GLint imageW = 0, imageH = 0;
    glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_WIDTH, &imageW);
    glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_HEIGHT, &imageH);
    if (imageW <= 0 || imageH <= 0)
      throw std::runtime_error("Item Lab UV texture has invalid dimensions");
    const float aspect = float(imageW) / imageH;
    float tileW = width - 2 * pad, tileH = height - 2 * pad;
    if (tileW / tileH > aspect)
      tileW = tileH * aspect;
    else
      tileH = tileW / aspect;
    const auto matrix =
        projection *
        glm::translate(
            glm::mat4(1),
            glm::vec3((tile % columns) * width + (width - tileW) * .5f,
                      1 - (tile / columns) * height - (height - tileH) * .5f,
                      0)) *
        glm::scale(glm::mat4(1), glm::vec3(tileW, -tileH, 1));
    // glTF UV origin is at the image's top-left. Keep the atlas and its
    // triangle coordinates in the same convention, including embedded images.
    std::vector<Vertex> quad;
    for (const auto &uv : {glm::vec2(0, 0), glm::vec2(1, 0), glm::vec2(1, 1),
                           glm::vec2(0, 0), glm::vec2(1, 1), glm::vec2(0, 1)})
      quad.push_back({{uv, 0}, {1, 1, 1, 1}, uv});
    if (!Draw(quad, GL_TRIANGLES, matrix, texture))
      return 0;
    const auto lines = ItemLabUvLines(asset, path);
    if (!Lines(lines, matrix))
      return 0;
    lineVertices += lines.size();
    ++tile;
  }
  return lineVertices;
}
} // namespace cutum
