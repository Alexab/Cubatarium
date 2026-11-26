#pragma once

#include "Render/Engine/GeometryEngine.h"
#include "Render/Engine/TerrainGpuBufferReport.h"
#include "Render/Engine/ViewEngine.h"
#include "Test/CreatureItemIconStorageFixture.h"
#include "World/Core/World.h"

namespace cutum
{
struct GeometryGpuStorageProbe
{
  static constexpr std::array Initializers{
      &UGeometryEngine::InitCubeBuffers, &UGeometryEngine::InitFaceQuadBuffers,
      &UGeometryEngine::InitPreviewBuffers, &UGeometryEngine::InitOutlineBuffers};

  static void DrawBatch(UGeometryEngine &engine, const RenderBatch &batch, bool faceQuads)
  {
    engine.Render.GreedyMeshing = engine.Render.FaceQuads = faceQuads;
    engine.DrawBatch(batch, glm::mat4(1.f));
  }

  static void DrawCrosshair(UGeometryEngine &engine, int width, int height)
  {
    engine.RenderCrosshair(width, height);
  }
};
} // namespace cutum

namespace cutum::geometry_gpu_storage_fixture
{
using Role = TerrainGpuBufferRole;
using gui_context_fixture::Require;
using shared_content_preview_fixture::Binding;

struct RejectUpload
{
  RejectUpload(GLenum target, GLenum usage) : Original(__glewBufferData), Target(target), Usage(usage)
  {
    Active = this;
    __glewBufferData = Upload;
  }
  ~RejectUpload() { __glewBufferData = Original; Active = nullptr; }
  std::array<GLuint, 3> observed{};

private:
  PFNGLBUFFERDATAPROC Original;
  GLenum Target, Usage;
  static inline RejectUpload *Active{};

  static void GLAPIENTRY Upload(GLenum target, GLsizeiptr bytes, const void *source, GLenum usage)
  {
    auto &test = *Active;
    if (!test.observed[0] && bytes > 0 && target == test.Target && usage == test.Usage)
    {
      test.observed = {GLuint(Binding(GL_VERTEX_ARRAY_BINDING)), GLuint(Binding(GL_ARRAY_BUFFER_BINDING)),
                       GLuint(Binding(GL_ELEMENT_ARRAY_BUFFER_BINDING))};
      bytes = -1;
    }
    test.Original(target, bytes, source, usage);
  }
};

inline nlohmann::json Run()
{
  creature_item_icon_fixture::PlatformFixture platform;
  const UGlStateScope state(kGlMaskFpViewmodel3D);
  auto world = std::make_shared<UWorld>(nullptr, std::make_shared<UViewEngine>());
  Require(world->AddUser("geometry_fixture") && world->GetCurrentUserCamera(), "bind the geometry draw camera");
  auto engine = std::make_unique<UGeometryEngine>(world, nullptr, nullptr);
  const auto collect = [&] {
    TerrainGpuBufferCensus census;
    engine->InspectGeometryGpuBuffers(census);
    return census;
  };
  const auto bytes = [](const TerrainGpuBufferSnapshot &s, Role role) { return s.roles[size_t(role)].bytes; };
  Require(!collect().Read().references, "no persistent geometry buffers before initialization");

  const GLint previousVao = Binding(GL_VERTEX_ARRAY_BINDING);
  const GLint previousArray = Binding(GL_ARRAY_BUFFER_BINDING);
  const GLint previousCopy = Binding(GL_COPY_READ_BUFFER_BINDING);
  GLuint sentinelVao{}, sentinelBuffer{}, query{};
  glGenVertexArrays(1, &sentinelVao);
  glGenBuffers(1, &sentinelBuffer);
  glGenQueries(1, &query);
  glBindVertexArray(sentinelVao);
  glBindBuffer(GL_ARRAY_BUFFER, sentinelBuffer);
  glBindBuffer(GL_COPY_READ_BUFFER, sentinelBuffer);
  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, sentinelBuffer);
  glBufferData(GL_ARRAY_BUFFER, 32, nullptr, GL_STATIC_DRAW);

  const auto preserved = [&] {
    return Binding(GL_VERTEX_ARRAY_BINDING) == GLint(sentinelVao) &&
           Binding(GL_ARRAY_BUFFER_BINDING) == GLint(sentinelBuffer) &&
           Binding(GL_ELEMENT_ARRAY_BUFFER_BINDING) == GLint(sentinelBuffer) &&
           Binding(GL_COPY_READ_BUFFER_BINDING) == GLint(sentinelBuffer);
  };
  for (const auto init : GeometryGpuStorageProbe::Initializers)
  {
    RejectUpload reject(GL_ELEMENT_ARRAY_BUFFER, GL_STATIC_DRAW);
    Require(!(engine.get()->*init)(), "geometry initializer rejects an incomplete indexed upload");
    const auto &names = reject.observed;
    Require(names[0] && names[1] && names[2] && !glIsVertexArray(names[0]) && !glIsBuffer(names[1]) &&
                !glIsBuffer(names[2]) && !collect().Read().references && preserved(),
            "rejected geometry names retire and the caller keeps all bindings");
  }

  for (const auto index : {0, 2, 3})
    Require((engine.get()->*GeometryGpuStorageProbe::Initializers[index])() && preserved(),
            "retry the static geometry initializer and preserve the caller bindings");
  const auto base = collect().Read();
  Require(base.total.objects == 7 && base.total.bytes == 1440, "measure static cube, preview and outline");
  Require(bytes(base, Role::GeometryCubeVertices) == 480 && bytes(base, Role::GeometryCubeIndices) == 144 &&
              bytes(base, Role::GeometryPreviewVertices) == 480 && bytes(base, Role::GeometryPreviewIndices) == 144 &&
              bytes(base, Role::GeometryOutlineVertices) == 96 && bytes(base, Role::GeometryOutlineIndices) == 96,
          "each static geometry store matches its independently expected layout");
  Require(engine->InitEngine() && collect().Read().total.objects == base.total.objects,
          "initialize the real geometry engine and retain the successfully retried buffers");
  glBindVertexArray(sentinelVao);
  glBindBuffer(GL_ARRAY_BUFFER, sentinelBuffer);
  glBindBuffer(GL_COPY_READ_BUFFER, sentinelBuffer);

  PreviewRenderTarget target;
  Require(target.Initialize(96), "create real geometry draw target");
  glBindFramebuffer(GL_FRAMEBUFFER, target.Framebuffer);
  glViewport(0, 0, 96, 96);
  const auto primitives = [&](const auto &draw) {
    glBeginQuery(GL_PRIMITIVES_GENERATED, query);
    draw();
    glEndQuery(GL_PRIMITIVES_GENERATED);
    GLuint result{};
    glGetQueryObjectuiv(query, GL_QUERY_RESULT, &result);
    return result;
  };
  RenderBatch batch;
  batch.ModelMatrices = {glm::mat4(1.f), glm::mat4(1.f)};
  batch.faceIndices = {0, 1};
  const auto draw = [&](bool face) { return primitives([&] { GeometryGpuStorageProbe::DrawBatch(*engine, batch, face); }); };
  Require(draw(false) == 24 && preserved(), "draw two cube instances while preserving bindings");
  const auto cube = collect().Read();
  Require(bytes(cube, Role::GeometryCubeInstances) == 128, "cube instance store holds exactly two matrices");
  Require(draw(true) == 4 && preserved(), "draw two face instances through the lazy face owner");
  const auto face = collect().Read();
  Require(bytes(face, Role::GeometryFaceVertices) == 80 && bytes(face, Role::GeometryFaceIndices) == 24 &&
              bytes(face, Role::GeometryFaceInstances) == 160, "measure the face layout and its current instance store");
  {
    RejectUpload reject(GL_ARRAY_BUFFER, GL_DYNAMIC_DRAW);
    Require(draw(true) == 0 && reject.observed[1] && preserved() &&
                bytes(collect().Read(), Role::GeometryFaceInstances) == 160,
            "rejected same-size upload preserves the old store and issues zero draws");
  }
  Require(draw(true) == 4 && preserved(), "retry the same face batch after refusal");
  batch.ModelMatrices.pop_back();
  batch.faceIndices.pop_back();
  Require(draw(true) == 2 && bytes(collect().Read(), Role::GeometryFaceInstances) == 80, "shrink the same face instance store");

  const auto crosshair = [&](int width, int height) {
    return primitives([&] { GeometryGpuStorageProbe::DrawCrosshair(*engine, width, height); });
  };
  {
    RejectUpload reject(GL_ARRAY_BUFFER, GL_DYNAMIC_DRAW);
    Require(crosshair(96, 96) == 0 && reject.observed[1] && !glIsBuffer(reject.observed[1]) && preserved(),
            "rejected first crosshair upload retires its candidate and keeps bindings");
  }
  Require(crosshair(96, 96) == 4 && preserved(), "draw both original crosshair strips");
  const auto liveNames = collect();
  const auto live = liveNames.Read();
  Require(bytes(live, Role::GeometryCrosshairVertices) == 64 && crosshair(84, 84) == 4 &&
              liveNames.Read().total.bytes == live.total.bytes && collect().Read().total.objects == live.total.objects && preserved(),
          "crosshair resize reuses the same 64-byte store");

  TerrainGpuBufferCensus aliasPreview, aliasGeometry;
  aliasPreview.Add(Role::PreviewCubeVertices, sentinelBuffer);
  aliasGeometry.Add(Role::GeometryCubeVertices, sentinelBuffer);
  const auto joined = DescribeSelectedGpuBuffers({&aliasPreview, &aliasGeometry});
  Require(joined.at("unique_objects") == 1 && joined.at("logical_bytes") == 32 &&
              joined.at("references") == 2 && joined.at("shared_across_roles") == 1 && preserved(),
          "joined census measures the same GL name once across separate owners");

  engine->SetRenderSettings(RenderSettings{});
  Require(liveNames.Read().namesWithoutObject == 3 && !bytes(collect().Read(), Role::GeometryFaceVertices),
          "changing render settings retires exactly the three face buffers");
  engine.reset();
  const auto retired = liveNames.Read();
  Require(retired.namesWithoutObject == live.total.objects && preserved() && glGetError() == GL_NO_ERROR,
          "geometry shutdown retires all its measured stores and leaves caller bindings intact");
  glBindVertexArray(GLuint(previousVao));
  glBindBuffer(GL_ARRAY_BUFFER, GLuint(previousArray));
  glBindBuffer(GL_COPY_READ_BUFFER, GLuint(previousCopy));
  glDeleteQueries(1, &query);
  glDeleteVertexArrays(1, &sentinelVao);
  glDeleteBuffers(1, &sentinelBuffer);
  return {{"status", "PASS"}, {"base", DescribeGeometryGpuBuffers(base)}, {"cube", DescribeGeometryGpuBuffers(cube)},
          {"face", DescribeGeometryGpuBuffers(face)}, {"live", DescribeGeometryGpuBuffers(live)},
          {"retired_names", retired.namesWithoutObject}, {"refused_initializers", GeometryGpuStorageProbe::Initializers.size()},
          {"same_size_refusal_retried", true}, {"alias", joined}};
}
} // namespace cutum::geometry_gpu_storage_fixture
