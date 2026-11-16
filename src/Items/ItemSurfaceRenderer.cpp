#include "Items/ItemSurfaceRenderer.h"

#include "Items/ItemGltfTextureCache.h"
#include "Render/Engine/ShaderManager.h"
#include "Render/GlIncludes.h"
#include <limits>
#include <stdexcept>

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
  {
    auto &buffer = entry.second.GeometryBuffer;
    glDeleteBuffers(1, &buffer.Vbo);
    glDeleteBuffers(1, &buffer.Ebo);
    glDeleteVertexArrays(1, &buffer.Vao);
  }
  Geometry.clear();
  BatchProfile.reset();
  BatchMaterial.reset();
}

UItemSurfaceBatchScope::UItemSurfaceBatchScope(ItemSurfaceRenderer &renderer)
    : Renderer(renderer)
{
  if (Renderer.BatchActive)
    throw std::logic_error("Nested item draw batches are not supported");
  Renderer.BatchActive = true;
  Renderer.BatchProfile.reset();
  Renderer.BatchMaterial.reset();
}
UItemSurfaceBatchScope::~UItemSurfaceBatchScope()
{
  Renderer.BatchActive = false;
  Renderer.BatchProfile.reset();
  Renderer.BatchMaterial.reset();
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
    // One buffer set per asset avoids hundreds of tiny driver allocations.
    // Local vertex values and primitive draw order remain unchanged; only
    // indices are rebased into the concatenated vertex stream.
    std::vector<float> vertices;
    std::vector<unsigned> indices;
    for (const auto &primitive : asset->Primitives)
    {
      const auto base = vertices.size() / 8;
      uploaded.IndexByteOffsets.push_back(indices.size() * sizeof(unsigned));
      for (const auto index : primitive.Indices)
      {
        if (base > std::numeric_limits<unsigned>::max() - index)
          throw std::runtime_error("Item vertex index exceeds GPU range");
        indices.push_back(unsigned(base) + index);
      }
      vertices.insert(vertices.end(), primitive.Vertices.begin(),
                      primitive.Vertices.end());
    }
    auto &buffer = uploaded.GeometryBuffer;
    glGenVertexArrays(1, &buffer.Vao);
    glGenBuffers(1, &buffer.Vbo);
    glGenBuffers(1, &buffer.Ebo);
    glBindVertexArray(buffer.Vao);
    glBindBuffer(GL_ARRAY_BUFFER, buffer.Vbo);
    glBufferData(GL_ARRAY_BUFFER, GLsizeiptr(vertices.size() * sizeof(float)),
                 vertices.data(), GL_STATIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, buffer.Ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER,
                 GLsizeiptr(indices.size() * sizeof(unsigned)), indices.data(),
                 GL_STATIC_DRAW);
    for (unsigned i = 0; i < 3; ++i)
    {
      const unsigned offsets[] = {0, 3, 5};
      const int widths[] = {3, 2, 3};
      glVertexAttribPointer(
          i, widths[i], GL_FLOAT, GL_FALSE, 8 * sizeof(float),
          reinterpret_cast<void *>(size_t(offsets[i]) * sizeof(float)));
      glEnableVertexAttribArray(i);
    }
    found = Geometry.emplace(asset.get(), std::move(uploaded)).first;
  }
  Shader->Use();
  const auto *previous = BatchActive && BatchProfile ? &*BatchProfile : nullptr;
  if (!previous)
    Shader->SetInt("uAlbedo", 0);
  if (!previous || previous->InspectionMode != profile.InspectionMode)
    Shader->SetInt("uInspectionMode", profile.InspectionMode);
#if !defined(__ANDROID__)
  GLint polygonMode[2] = {GL_FILL, GL_FILL};
  if (profile.InspectionMode == 6)
  {
    glGetIntegerv(GL_POLYGON_MODE, polygonMode);
    glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
  }
#endif
  if (!previous || previous->CameraPosition != profile.CameraPosition)
    Shader->SetVec3("uCamera", profile.CameraPosition);
  if (!previous || previous->LightDirection != profile.LightDirection)
    Shader->SetVec3("uLightDirection", profile.LightDirection);
  if (!previous || previous->Ambient != profile.Ambient)
    Shader->SetFloat("uAmbient", profile.Ambient);
  if (!previous || previous->LightIntensity != profile.LightIntensity)
    Shader->SetFloat("uLightIntensity", profile.LightIntensity);
  if (!previous || previous->Fog != profile.Fog)
    Shader->SetFloat("uFog", profile.Fog);
  if (!previous || previous->FogColor != profile.FogColor)
    Shader->SetVec3("uFogColor", profile.FogColor);
  if (BatchActive)
    BatchProfile = profile;
  const auto transforms = state.NodeTransforms.empty()
                              ? std::vector<glm::mat4>{}
                              : ResolveItemNodeTransforms(*asset, state);
  const ItemStaticWearSection *selection = nullptr;
  if (state.NodeTransforms.empty() && state.HiddenNodes.empty() &&
      state.VisibleRoots.size() == 1)
  {
    const auto foundSection =
        asset->StaticWearSections.find(*state.VisibleRoots.begin());
    if (foundSection != asset->StaticWearSections.end())
      selection = &foundSection->second;
  }
  std::optional<MaterialUniforms> localMaterial;
  size_t drawn = 0;
  size_t lastNode = size_t(-1);
  unsigned lastTexture = 0;
  const auto count =
      selection ? selection->Primitives.size() : asset->Primitives.size();
  glBindVertexArray(found->second.GeometryBuffer.Vao);
  for (size_t entry = 0; entry < count; ++entry)
  {
    const size_t i = selection ? selection->Primitives[entry] : entry;
    const auto &primitive = asset->Primitives[i];
    if (!selection && !ItemNodeVisible(*asset, state, primitive.Node))
      continue;
    const glm::mat4 matrix =
        model * (transforms.empty() ? asset->Nodes[primitive.Node].Transform
                                    : transforms[primitive.Node]);
    const auto &material = asset->Materials[primitive.Material];
    const auto texture =
        ItemGltfTextureCache::Instance().GetLinearAlbedo(material.Albedo);
    if (!texture)
      continue;
    if (lastNode != primitive.Node)
    {
      Shader->SetMat4("uModel", matrix);
      Shader->SetMat4("uMvp", viewProjection * matrix);
      lastNode = primitive.Node;
    }
    const auto &cachedMaterial = BatchActive ? BatchMaterial : localMaterial;
    const auto *last = cachedMaterial ? &*cachedMaterial : nullptr;
    if (!last || last->Color != material.Color)
      Shader->SetVec4("uColor", material.Color);
    if (!last || last->Metallic != material.Metallic)
      Shader->SetFloat("uMetallic", material.Metallic);
    if (!last || last->Roughness != material.Roughness)
      Shader->SetFloat("uRoughness", material.Roughness);
    if (!last || last->Emissive != material.Emissive)
      Shader->SetVec3("uEmissive", material.Emissive);
    auto &savedMaterial = BatchActive ? BatchMaterial : localMaterial;
    savedMaterial = MaterialUniforms{material.Color, material.Emissive,
                                     material.Metallic, material.Roughness};
    if (lastTexture != texture)
    {
      glActiveTexture(GL_TEXTURE0);
      glBindTexture(GL_TEXTURE_2D, texture);
      lastTexture = texture;
    }
    ++drawn;
    glDrawElements(GL_TRIANGLES, GLsizei(primitive.Indices.size()),
                   GL_UNSIGNED_INT,
                   reinterpret_cast<void *>(found->second.IndexByteOffsets[i]));
  }
  if (UItemSurfaceDrawAuditScope::Current)
    UItemSurfaceDrawAuditScope::Current->push_back({path, asset, drawn, model});
  glBindVertexArray(0);
#if !defined(__ANDROID__)
  if (profile.InspectionMode == 6)
    glPolygonMode(GL_FRONT_AND_BACK, polygonMode[0]);
#endif
  Shader->Unuse();
}
} // namespace cutum
