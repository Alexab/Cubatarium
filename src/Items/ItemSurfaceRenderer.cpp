#include "Items/ItemSurfaceRenderer.h"

#include "Items/ItemGltfTextureCache.h"
#include "Render/Engine/ShaderManager.h"
#include "Render/GlIncludes.h"

namespace cutum
{
ItemSurfaceRenderer::~ItemSurfaceRenderer() { Clear(); }

bool ItemSurfaceRenderer::Initialize(UShaderManager &manager)
{
  Shader = manager.CreateShader("item_surface", "shaders/item_surface.vert",
                                "shaders/item_surface.frag");
  return Shader && Shader->IsValid();
}

void ItemSurfaceRenderer::Clear()
{
  for (auto &entry : Geometry)
    for (auto &buffer : entry.second.Primitives)
    {
      glDeleteBuffers(1, &buffer.Vbo);
      glDeleteBuffers(1, &buffer.Ebo);
      glDeleteVertexArrays(1, &buffer.Vao);
    }
  Geometry.clear();
  FilteredTextures.clear();
}

void ItemSurfaceRenderer::Draw(
    const std::shared_ptr<const ItemRenderAsset> &asset,
    const std::filesystem::path &path, const glm::mat4 &viewProjection,
    const glm::mat4 &model, const ItemVisualState &state,
    const ItemPresentationProfile &profile)
{
  if (!asset || !Shader || !Shader->IsValid())
    return;
  auto found = Geometry.find(asset.get());
  if (found == Geometry.end())
  {
    Uploaded uploaded;
    uploaded.Asset = asset;
    for (const auto &primitive : asset->Primitives)
    {
      Buffer buffer;
      glGenVertexArrays(1, &buffer.Vao);
      glGenBuffers(1, &buffer.Vbo);
      glGenBuffers(1, &buffer.Ebo);
      glBindVertexArray(buffer.Vao);
      glBindBuffer(GL_ARRAY_BUFFER, buffer.Vbo);
      glBufferData(GL_ARRAY_BUFFER,
                   GLsizeiptr(primitive.Vertices.size() * sizeof(float)),
                   primitive.Vertices.data(), GL_STATIC_DRAW);
      glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, buffer.Ebo);
      glBufferData(GL_ELEMENT_ARRAY_BUFFER,
                   GLsizeiptr(primitive.Indices.size() * sizeof(unsigned)),
                   primitive.Indices.data(), GL_STATIC_DRAW);
      for (unsigned i = 0; i < 3; ++i)
      {
        const unsigned offsets[] = {0, 3, 5};
        const int widths[] = {3, 2, 3};
        glVertexAttribPointer(
            i, widths[i], GL_FLOAT, GL_FALSE, 8 * sizeof(float),
            reinterpret_cast<void *>(size_t(offsets[i]) * sizeof(float)));
        glEnableVertexAttribArray(i);
      }
      uploaded.Primitives.push_back(buffer);
    }
    found = Geometry.emplace(asset.get(), std::move(uploaded)).first;
  }
  Shader->Use();
  Shader->SetInt("uAlbedo", 0);
  Shader->SetInt("uInspectionMode", profile.InspectionMode);
#if !defined(__ANDROID__)
  GLint polygonMode[2] = {GL_FILL, GL_FILL};
  if (profile.InspectionMode == 6)
  {
    glGetIntegerv(GL_POLYGON_MODE, polygonMode);
    glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
  }
#endif
  Shader->SetVec3("uCamera", profile.CameraPosition);
  Shader->SetVec3("uLightDirection", profile.LightDirection);
  Shader->SetFloat("uAmbient", profile.Ambient);
  Shader->SetFloat("uLightIntensity", profile.LightIntensity);
  Shader->SetFloat("uFog", profile.Fog);
  Shader->SetVec3("uFogColor", profile.FogColor);
  const auto transforms = ResolveItemNodeTransforms(*asset, state);
  for (size_t i = 0; i < asset->Primitives.size(); ++i)
  {
    const auto &primitive = asset->Primitives[i];
    const glm::mat4 matrix = model * transforms[primitive.Node];
    const auto &material = asset->Materials[primitive.Material];
    const auto texture =
        ItemGltfTextureCache::Instance().Get(path, material.Name);
    if (!texture)
      continue;
    Shader->SetMat4("uModel", matrix);
    Shader->SetMat4("uMvp", viewProjection * matrix);
    Shader->SetVec4("uColor", material.Color);
    Shader->SetFloat("uMetallic", material.Metallic);
    Shader->SetFloat("uRoughness", material.Roughness);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, texture);
    if (FilteredTextures.insert(texture).second)
    {
      glGenerateMipmap(GL_TEXTURE_2D);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER,
                      GL_LINEAR_MIPMAP_LINEAR);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    }
    glBindVertexArray(found->second.Primitives[i].Vao);
    glDrawElements(GL_TRIANGLES, GLsizei(primitive.Indices.size()),
                   GL_UNSIGNED_INT, nullptr);
  }
  glBindVertexArray(0);
#if !defined(__ANDROID__)
  if (profile.InspectionMode == 6)
    glPolygonMode(GL_FRONT_AND_BACK, polygonMode[0]);
#endif
  Shader->Unuse();
}
} // namespace cutum
