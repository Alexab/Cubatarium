#pragma once

#include "Gui/Preview/ItemLabOverlayRenderer.h"
#include "Items/ItemRenderAsset.h"
#include "Render/Engine/TerrainGpuBufferReport.h"
#include "Test/CreatureItemIconStorageFixture.h"
#include <fstream>

namespace cutum::preview_gpu_storage_fixture
{
using gui_context_fixture::Require;
using shared_content_preview_fixture::Binding;
using Json = nlohmann::json;

inline void WriteLegacy(const std::filesystem::path &path, bool quad)
{
  const float positions[]{-.5f,-.5f,0, .5f,-.5f,0, 0,.5f,0, -.5f,.5f,0};
  const uint16_t indices[]{0,1,2, 0,2,3};
  const auto vertexCount = quad ? 4 : 3;
  const auto indexCount = quad ? 6 : 3;
  const auto vertexBytes = vertexCount * 3 * sizeof(float);
  const auto indexBytes = indexCount * sizeof(uint16_t);
  auto binary = path.filename();
  binary.replace_extension(".bin");
  std::ofstream mesh(path.parent_path() / binary, std::ios::binary);
  mesh.write(reinterpret_cast<const char *>(positions), vertexBytes);
  mesh.write(reinterpret_cast<const char *>(indices), indexBytes);
  Require(bool(mesh), "write independent preview vertex and index sources");
  std::ofstream model(path);
  model << Json{{"asset", {{"version", "2.0"}}},
      {"buffers", Json::array({{{"uri", binary.string()}, {"byteLength", vertexBytes + indexBytes}}})},
      {"bufferViews", Json::array({{{"buffer", 0}, {"byteLength", vertexBytes}},
                                    {{"buffer", 0}, {"byteOffset", vertexBytes}, {"byteLength", indexBytes}}})},
      {"accessors", Json::array({{{"bufferView", 0}, {"componentType", 5126}, {"type", "VEC3"}, {"count", vertexCount}},
                                  {{"bufferView", 1}, {"componentType", 5123}, {"type", "SCALAR"}, {"count", indexCount}}})},
      {"meshes", Json::array({{{"primitives", Json::array({{{"attributes", {{"POSITION", 0}}}, {"indices", 1}}})}}})}};
  Require(bool(model), "write legacy preview glTF data");
}

struct RefuseStreamUpload
{
  explicit RefuseStreamUpload(GLenum target) { Original = __glewBufferData; Target = target; __glewBufferData = Upload; }
  ~RefuseStreamUpload() { __glewBufferData = Original; }
  static inline bool refused{};

private:
  static inline PFNGLBUFFERDATAPROC Original{};
  static inline GLenum Target{};
  static void GLAPIENTRY Upload(GLenum target, GLsizeiptr bytes, const void *source, GLenum usage)
  {
    if (target == Target && usage == GL_STREAM_DRAW && !refused)
    {
      refused = true;
      Original(target, -1, source, usage);
    }
    else
      Original(target, bytes, source, usage);
  }
};

inline Json Run(const std::filesystem::path &reportPath)
{
  creature_item_icon_fixture::PlatformFixture platform;
  const auto root = std::filesystem::absolute(reportPath.string() + ".preview-gpu");
  Require(std::filesystem::create_directory(root), "create isolated preview GPU fixtures");
  WriteLegacy(root / "triangle.gltf", false);
  WriteLegacy(root / "quad.gltf", true);
  for (const auto *id : {"triangle", "quad"})
  {
    std::ofstream row(root / (std::string(id) + ".json"));
    row << Json{{"id", id}, {"model", (root / (std::string(id) + ".gltf")).generic_string()}};
    Require(bool(row), "register legacy preview input");
  }
  auto items = std::make_shared<UItemDefinitionStorage>();
  items->Load(root.string());
  auto shaders = std::make_shared<UShaderManager>();
  auto item = UItemPreviewRenderer::Create(items, shaders);
  Require(item->Initialize(), "initialize real item preview resources");
  const auto collect = [&] {
    TerrainGpuBufferCensus out;
    item->InspectGpuBuffers(out);
    return out;
  };
  const auto size = [](const TerrainGpuBufferSnapshot &s, TerrainGpuBufferRole role) {
    return s.roles[static_cast<size_t>(role)].bytes;
  };
  const GLint previousArray = Binding(GL_ARRAY_BUFFER_BINDING);
  const GLint previousRead = Binding(GL_COPY_READ_BUFFER_BINDING);
  GLuint sentinel{}, query{};
  glGenBuffers(1, &sentinel);
  glBindBuffer(GL_ARRAY_BUFFER, sentinel);
  glBindBuffer(GL_COPY_READ_BUFFER, sentinel);
  glBufferData(GL_ARRAY_BUFFER, 16, nullptr, GL_STATIC_DRAW);
  glGenQueries(1, &query);
  const auto draw = [&](const char *id, int extent) {
    glBeginQuery(GL_PRIMITIVES_GENERATED, query);
    UGlTextureOwner output(item->RenderToUniqueTexture(id, extent, 35.f, 25.f));
    glEndQuery(GL_PRIMITIVES_GENERATED);
    GLuint primitives{};
    glGetQueryObjectuiv(query, GL_QUERY_RESULT, &primitives);
    Require(output.Get() && glIsTexture(output.Get()) && Binding(GL_ARRAY_BUFFER_BINDING) == GLint(sentinel),
            "preview draw returns a live image and preserves the caller array binding");
    return primitives;
  };
  const auto initialized = collect().Read();
  Require(initialized.total.objects == 2 && initialized.total.bytes == 624, "preview cube buffers match authored extents");
  Require(draw("triangle", 96) == 1, "legacy triangle takes the actual indexed scratch draw");
  const auto triangle = collect().Read();
  Require(size(triangle, TerrainGpuBufferRole::PreviewScratchVertices) == 60 &&
              size(triangle, TerrainGpuBufferRole::PreviewScratchIndices) == 12, "scratch reports one triangle");
  {
    RefuseStreamUpload::refused = false;
    RefuseStreamUpload refuse(GL_ELEMENT_ARRAY_BUFFER);
    Require(draw("triangle", 96) == 12 && RefuseStreamUpload::refused,
            "same-size rejected index write uses the fallback cube and skips the old scratch store");
  }
  const auto refused = collect().Read();
  Require(refused.total.bytes == triangle.total.bytes && draw("triangle", 96) == 1 &&
              glGetError() == GL_NO_ERROR, "same-size rejection retains storage and real GL recovery succeeds");
  Require(draw("quad", 128) == 2, "legacy preview draws two indexed triangles after growth");
  const auto grown = collect().Read();
  Require(size(grown, TerrainGpuBufferRole::PreviewScratchVertices) == 80 &&
              size(grown, TerrainGpuBufferRole::PreviewScratchIndices) == 24 && draw("triangle", 80) == 1,
          "scratch grows and accepts smaller replacement");
  Require(collect().Read().total.bytes == triangle.total.bytes, "scratch census reflects smaller replacement");

  const auto bucket = IUPlatformPaths::Current().AssetRoot() / "models/items/bucket/model.gltf";
  const auto asset = item->GetInspectionAsset(bucket);
  size_t vertexBytes{}, indexBytes{};
  for (const auto &primitive : asset->Primitives)
  {
    vertexBytes += primitive.Vertices.size() * sizeof(float);
    indexBytes += primitive.Indices.size() * sizeof(unsigned);
  }
  UGlTextureOwner rendered(item->RenderInspectionTexture("bucket", bucket, 96, 35.f, 25.f, 0, .65f));
  const auto actual = collect();
  const auto withSurface = actual.Read();
  Require(rendered.Get() && vertexBytes && indexBytes &&
              size(withSurface, TerrainGpuBufferRole::PreviewSurfaceVertices) == vertexBytes &&
              size(withSurface, TerrainGpuBufferRole::PreviewSurfaceIndices) == indexBytes,
          "inspection uploads the complete authored surface alongside legacy scratch");
  Require(Binding(GL_COPY_READ_BUFFER_BINDING) == GLint(sentinel), "buffer census preserves copy-read binding");

  TerrainGpuBufferCensus overlayCensus;
  size_t contacts{};
  uint64_t overlayBytes{};
  {
    UItemLabOverlayRenderer overlay(*shaders);
    overlay.InspectGpuBuffers(overlayCensus);
    Require(overlayCensus.Read().total.objects == 1 && !overlayCensus.Read().total.bytes, "cold overlay has an empty store");
    const auto state = EvaluateItemVisualState(*asset, {});
    const auto extent = glm::vec3(.25f);
    const auto expected = ItemLabContactLines(*asset, state, extent).size();
    contacts = overlay.DrawContacts(*asset, state, extent, glm::mat4(1.f));
    overlayBytes = overlayCensus.Read().total.bytes;
    Require(contacts == expected && contacts && overlayBytes == contacts * 9 * sizeof(float), "contact draw uses all diagnostic vertices");
    RefuseStreamUpload::refused = false;
    {
      RefuseStreamUpload refuse(GL_ARRAY_BUFFER);
      Require(!overlay.DrawContacts(*asset, state, extent, glm::mat4(1.f)) && RefuseStreamUpload::refused,
              "overlay rejects an unchanged-size failed upload");
    }
    Require(overlay.DrawContacts(*asset, state, extent, glm::mat4(1.f)) == contacts,
            "overlay retries successfully with the same extent");
  }
  Require(overlayCensus.Read().namesWithoutObject == 1, "overlay owner retires the actual buffer name");
  item->Invalidate();
  const auto invalidated = collect().Read();
  Require(invalidated.total.objects == 2 && actual.Read().namesWithoutObject == 4 && glIsTexture(rendered.Get()),
          "preview invalidation retires scratch and authored surfaces and preserves the caller-owned image");
  const auto cube = collect();
  item->Shutdown();
  Require(!collect().Read().references && cube.Read().namesWithoutObject == 2 && glIsTexture(rendered.Get()) &&
              glGetError() == GL_NO_ERROR, "preview shutdown retires both cube stores and preserves owned output");
  glDeleteQueries(1, &query);
  glBindBuffer(GL_ARRAY_BUFFER, GLuint(previousArray));
  glBindBuffer(GL_COPY_READ_BUFFER, GLuint(previousRead));
  glDeleteBuffers(1, &sentinel);
  return {{"status", "PASS"}, {"triangle", DescribePreviewGpuBuffers(triangle)},
          {"grown", DescribePreviewGpuBuffers(grown)}, {"with_surface", DescribePreviewGpuBuffers(withSurface)},
          {"invalidated", DescribePreviewGpuBuffers(invalidated)}, {"surface_bytes", {vertexBytes, indexBytes}},
          {"contact_vertices", contacts}, {"overlay_bytes", overlayBytes}, {"same_size_refusal_retried", true},
          {"caller_bindings_preserved", true}, {"retirement_and_external_output", true}};
}
} // namespace cutum::preview_gpu_storage_fixture
