#pragma once

#include "Blocks/BlockDefinitionStorage.h"
#include "Blocks/BlockRegistry.h"
#include "Gui/Preview/ContentPreviewRenderer.h"
#include "Render/Engine/ShaderManager.h"
#include "Render/Pipeline/GlStateScope.h"
#include "Render/Textures/GlTextureOwner.h"
#include "Render/Textures/TextureCube.h"
#include "ResourcePacks/BlockMergeRegistry.h"
#include "Test/GuiContextStorageFixture.h"
#include "Test/IconLifecycleFixture.h"
#include "Test/OrdinaryNewObservation.h"
#include "World/Objects/ObjectLibrary.h"
#include <array>
#include <filesystem>
#include <fstream>

namespace cutum::shared_content_preview_fixture
{
using gui_context_fixture::Require;
using Json = nlohmann::json;

inline GLint Binding(GLenum key)
{
  GLint value = 0;
  glGetIntegerv(key, &value);
  return value;
}

struct CallerState
{
  GLint draw, read, active, texture0, texture2, array, element, unpack, renderbuffer, vao, program;
  std::array<GLint, 4> viewport;
  std::array<GLfloat, 4> clear;
  GLboolean depthWrite;

  std::array<GLint, 11> Bindings() const
  {
    return {draw, read, active, texture0, texture2, array, element, unpack, renderbuffer, vao, program};
  }

  bool operator==(const CallerState &other) const
  {
    return Bindings() == other.Bindings() && viewport == other.viewport &&
           clear == other.clear && depthWrite == other.depthWrite;
  }
};

inline CallerState ReadCallerState()
{
  CallerState state{};
  state.draw = Binding(GL_DRAW_FRAMEBUFFER_BINDING);
  state.read = Binding(GL_READ_FRAMEBUFFER_BINDING);
  state.active = Binding(GL_ACTIVE_TEXTURE);
  glActiveTexture(GL_TEXTURE0);
  state.texture0 = Binding(GL_TEXTURE_BINDING_2D);
  glActiveTexture(GL_TEXTURE2);
  state.texture2 = Binding(GL_TEXTURE_BINDING_2D);
  glActiveTexture(GLenum(state.active));
  state.array = Binding(GL_ARRAY_BUFFER_BINDING);
  state.element = Binding(GL_ELEMENT_ARRAY_BUFFER_BINDING);
  state.unpack = Binding(GL_PIXEL_UNPACK_BUFFER_BINDING);
  state.renderbuffer = Binding(GL_RENDERBUFFER_BINDING);
  state.vao = Binding(GL_VERTEX_ARRAY_BINDING);
  state.program = Binding(GL_CURRENT_PROGRAM);
  glGetIntegerv(GL_VIEWPORT, state.viewport.data());
  glGetFloatv(GL_COLOR_CLEAR_VALUE, state.clear.data());
  glGetBooleanv(GL_DEPTH_WRITEMASK, &state.depthWrite);
  return state;
}

struct RefuseIndexUpload
{
  explicit RefuseIndexUpload(bool refuse = true)
  {
    Original = __glewBufferData;
    Observed = {};
    Refuse = refuse;
    __glewBufferData = Upload;
  }
  ~RefuseIndexUpload() { __glewBufferData = Original; }
  inline static std::array<GLuint, 3> Observed{};

private:
  inline static PFNGLBUFFERDATAPROC Original{};
  inline static bool Refuse{};
  static void GLAPIENTRY Upload(GLenum target, GLsizeiptr bytes, const void *data, GLenum usage)
  {
    if (target == GL_ELEMENT_ARRAY_BUFFER)
      Observed = {GLuint(Binding(GL_VERTEX_ARRAY_BINDING)), GLuint(Binding(GL_ARRAY_BUFFER_BINDING)),
                  GLuint(Binding(GL_ELEMENT_ARRAY_BUFFER_BINDING))};
    if (target != GL_ELEMENT_ARRAY_BUFFER || !Refuse)
      Original(target, bytes, data, usage);
  }
};

inline GLuint Attachment(GLenum point)
{
  GLint name = 0;
  glGetFramebufferAttachmentParameteriv(GL_DRAW_FRAMEBUFFER, point, GL_FRAMEBUFFER_ATTACHMENT_OBJECT_NAME, &name);
  return GLuint(name);
}

struct RefuseFramebuffer
{
  explicit RefuseFramebuffer(bool refuse = true)
  {
    Original = __glewCheckFramebufferStatus;
    Observed = {};
    Refuse = refuse;
    __glewCheckFramebufferStatus = Check;
  }
  ~RefuseFramebuffer() { __glewCheckFramebufferStatus = Original; }
  inline static std::array<GLuint, 3> Observed{};

private:
  inline static PFNGLCHECKFRAMEBUFFERSTATUSPROC Original{};
  inline static bool Refuse{};
  static GLenum GLAPIENTRY Check(GLenum target)
  {
    Observed = {GLuint(Binding(GL_DRAW_FRAMEBUFFER_BINDING)), Attachment(GL_COLOR_ATTACHMENT0),
                Attachment(GL_DEPTH_ATTACHMENT)};
    return Refuse ? GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT : Original(target);
  }
};

inline bool TargetIsRetired(const ContentPreviewRendererStorage &state)
{
  return !glIsFramebuffer(state.framebuffer) && !glIsTexture(state.colorTexture) &&
         !glIsRenderbuffer(state.depthBuffer);
}

inline void CheckTarget(const ContentPreviewRendererStorage &state, int size)
{
  Require(state.supported && state.targetSize == size && state.colorTexels == size_t(size) * size &&
              state.depthSamples == state.colorTexels && glIsFramebuffer(state.framebuffer) &&
              glIsTexture(state.colorTexture) && glIsRenderbuffer(state.depthBuffer),
          "preview source reports the live target and its extent");
  UGlStateScope restore(static_cast<GlStateMask>(GlStateBit::ViewportFb));
  glBindFramebuffer(GL_DRAW_FRAMEBUFFER, state.framebuffer);
  Require(glCheckFramebufferStatus(GL_DRAW_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE &&
              Attachment(GL_COLOR_ATTACHMENT0) == state.colorTexture && Attachment(GL_DEPTH_ATTACHMENT) == state.depthBuffer,
          "driver attachments match the published preview tuple");
  glBindTexture(GL_TEXTURE_2D, state.colorTexture);
  GLint width = 0, height = 0;
  glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_WIDTH, &width);
  glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_HEIGHT, &height);
  Require(width == size && height == size, "driver texture dimensions match the preview extent");
  const GLint renderbuffer = Binding(GL_RENDERBUFFER_BINDING);
  glBindRenderbuffer(GL_RENDERBUFFER, state.depthBuffer);
  GLint depthWidth = 0, depthHeight = 0, depthBits = 0;
  glGetRenderbufferParameteriv(GL_RENDERBUFFER, GL_RENDERBUFFER_WIDTH, &depthWidth);
  glGetRenderbufferParameteriv(GL_RENDERBUFFER, GL_RENDERBUFFER_HEIGHT, &depthHeight);
  glGetRenderbufferParameteriv(GL_RENDERBUFFER, GL_RENDERBUFFER_DEPTH_SIZE, &depthBits);
  glBindRenderbuffer(GL_RENDERBUFFER, GLuint(renderbuffer));
  Require(depthWidth == size && depthHeight == size && depthBits == 24,
          "driver depth dimensions and format match the preview request");
}

inline Json Run(const std::filesystem::path &reportPath)
{
  auto fixture = reportPath;
  fixture += ".preview-object";
  Require(std::filesystem::create_directory(fixture), "create isolated preview object fixture");
  const std::string blockName = "preview_fixture_block";
  const std::string objectName = "preview_fixture_object";
  {
    std::ofstream file(fixture / (objectName + ".json"));
    file << R"({"name":"preview_fixture_object","blocks":[{"dx":0,"dy":0,"dz":0,"type":"preview_fixture_block"}]})";
    Require(bool(file), "write isolated preview object data");
  }
  auto bases = std::make_shared<UTextureBaseStorage>();
  bases->RegisterPixels(blockName, {1, 1, {19, 109, 172, 255}});
  auto textures = std::make_shared<UTextureCubeStorage>(bases);
  auto blocks = std::make_shared<UBlockDefinitionStorage>();
  BlockDefinition block;
  block.Id = 71;
  block.Name = blockName;
  blocks->ReplaceAll({{block.Id, block}}, {{blockName, block.Id}});
  MergedCubeDesc cube;
  cube.Id = block.Id;
  cube.Name = blockName;
  cube.Stems.fill(blockName);
  textures->BuildFromDescriptors({cube});
  const GLuint sourceTexture = textures->GetTextures().at(block.Id).GetTexture();
  UBlockRegistry registry(textures, blocks);
  auto objects = std::make_shared<UObjectLibrary>();
  objects->Load(fixture.string(), registry);
  Require(sourceTexture && glIsTexture(sourceTexture) && objects->GetShared(objectName),
          "load independent preview block atlas and object fixture");
  auto shaders = std::make_shared<UShaderManager>();
  const auto inspect = [&](const UContentPreviewRenderer &renderer)
  {
    return renderer.InspectOwnedStorage(objects.get(), textures.get(), blocks.get(), shaders.get(), nullptr);
  };
  size_t shell = 0;
  {
    observation::Scope observed;
    auto renderer = std::make_unique<UContentPreviewRenderer>(objects, textures, blocks, shaders);
    const auto state = inspect(*renderer);
    shell = state.rendererBytes;
    Require(state.supported && state.bindingsMatch && !state.shaderPresent &&
                !state.vertexBytes && !state.targetSize && shell && observation::State.live == shell &&
                observation::State.attempts == 1, "preview owner shell is one independently observed allocation");
    Require(!renderer->InspectOwnedStorage(objects.get(), nullptr, blocks.get(), shaders.get(), nullptr).supported,
            "foreign atlas binding rejects source proof");
    renderer.reset();
    Require(!observation::State.live, "preview shell retires");
  }

  const auto original = ReadCallerState();
  UGlStateScope restore(kGlMaskIconFbo | GlStateBit::DepthMask);
  auto callerTexture0 = icon_lifecycle_fixture::MakeTexture();
  auto callerTexture2 = icon_lifecycle_fixture::MakeTexture();
  UGlFramebufferOwner callerDraw, callerRead;
  GLuint callerVao = 0, callerRenderbuffer = 0;
  std::array<GLuint, 3> callerBuffers{};
  glGenVertexArrays(1, &callerVao);
  glGenRenderbuffers(1, &callerRenderbuffer);
  glGenBuffers(GLsizei(callerBuffers.size()), callerBuffers.data());
  glBindVertexArray(callerVao);
  glBindBuffer(GL_ARRAY_BUFFER, callerBuffers[0]);
  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, callerBuffers[1]);
  glBindBuffer(GL_PIXEL_UNPACK_BUFFER, callerBuffers[2]);
  glBufferData(GL_PIXEL_UNPACK_BUFFER, 4, nullptr, GL_STATIC_DRAW);
  glBindRenderbuffer(GL_RENDERBUFFER, callerRenderbuffer);
  glBindFramebuffer(GL_DRAW_FRAMEBUFFER, callerDraw.Get());
  glBindFramebuffer(GL_READ_FRAMEBUFFER, callerRead.Get());
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, callerTexture0.Get());
  glActiveTexture(GL_TEXTURE2);
  glBindTexture(GL_TEXTURE_2D, callerTexture2.Get());
  glViewport(1, 2, 13, 19);
  glDepthMask(GL_FALSE);
  glClearColor(0.25f, 0.5f, 0.75f, 1.0f);
  const auto caller = ReadCallerState();

  UContentPreviewRenderer renderer(objects, textures, blocks, shaders);
  {
    RefuseIndexUpload refuse;
    Require(!renderer.Initialize(), "refused index upload prevents mesh publication");
  }
  const auto declined = RefuseIndexUpload::Observed;
  Require(declined[0] && declined[1] && declined[2] && !glIsVertexArray(declined[0]) &&
              !glIsBuffer(declined[1]) && !glIsBuffer(declined[2]) && !inspect(renderer).vertexBytes &&
              !inspect(renderer).shaderPresent && ReadCallerState() == caller,
          "incomplete cube retires all candidate names and restores caller bindings");
  Require(renderer.Initialize() && ReadCallerState() == caller, "cube retry preserves caller bindings");
  const auto mesh = inspect(renderer);
  Require(mesh.supported && mesh.vertexArray && mesh.shaderPresent && !mesh.targetSize,
          "initialized preview has a published cube and a lazy framebuffer");
  glBindVertexArray(mesh.vertexArray);
  glBindBuffer(GL_ARRAY_BUFFER, mesh.vertexBuffer);
  GLint vertexBytes = 0, indexBytes = 0;
  glGetBufferParameteriv(GL_ARRAY_BUFFER, GL_BUFFER_SIZE, &vertexBytes);
  glGetBufferParameteriv(GL_ELEMENT_ARRAY_BUFFER, GL_BUFFER_SIZE, &indexBytes);
  Require(vertexBytes == 480 && indexBytes == 144 && mesh.vertexBytes == size_t(vertexBytes) &&
              mesh.indexBytes == size_t(indexBytes) && GLuint(Binding(GL_ELEMENT_ARRAY_BUFFER_BINDING)) == mesh.indexBuffer,
          "real driver cube buffers match the retained requested bytes");
  glBindVertexArray(GLuint(caller.vao));
  glBindBuffer(GL_ARRAY_BUFFER, GLuint(caller.array));

  const GLuint shared64 = renderer.Render(ContentKind::Block, blockName, -1, 45.f, 32.f);
  const auto small = inspect(renderer);
  CheckTarget(small, 64);
  Require(shared64 == small.colorTexture && ReadCallerState() == caller &&
              renderer.Render(ContentKind::Object, objectName, 64, 45.f, 32.f) == shared64 &&
              inspect(renderer).framebuffer == small.framebuffer && ReadCallerState() == caller,
          "shared block and object draws reuse the small target and restore caller state");
  UGlTextureOwner unique64(renderer.RenderUnique(ContentKind::Object, objectName, 64, 45.f, 32.f));
  Require(unique64.Get() && unique64.Get() != shared64 && glIsTexture(unique64.Get()) && ReadCallerState() == caller,
          "owned object output is independent of the shared target");
  CheckTarget(inspect(renderer), 64);

  {
    RefuseFramebuffer refuse;
    Require(!renderer.Render(ContentKind::Block, blockName, 256, 45.f, 32.f), "refuse larger shared target");
  }
  const auto refused = RefuseFramebuffer::Observed;
  Require(refused[0] && refused[1] && refused[2] && refused[0] != small.framebuffer &&
              !glIsFramebuffer(refused[0]) && !glIsTexture(refused[1]) && !glIsRenderbuffer(refused[2]) &&
              inspect(renderer).framebuffer == small.framebuffer && ReadCallerState() == caller,
          "refused resize retires candidates and preserves the previous target and caller state");
  CheckTarget(inspect(renderer), 64);
  Require(renderer.Render(ContentKind::Object, objectName, 1024, 45.f, 32.f) != 0,
          "shared object draw retries growth at the maximum size");
  const auto large = inspect(renderer);
  CheckTarget(large, 512);
  Require(TargetIsRetired(small) && glIsTexture(unique64.Get()) && ReadCallerState() == caller,
          "growth retires only the previous shared target");

  {
    RefuseFramebuffer refuse;
    Require(!renderer.RenderUnique(ContentKind::Block, blockName, 512, 45.f, 32.f), "refuse temporary unique output");
  }
  const auto refusedOwned = RefuseFramebuffer::Observed;
  Require(refusedOwned[0] == large.framebuffer && refusedOwned[1] && refusedOwned[1] != large.colorTexture &&
              !glIsTexture(refusedOwned[1]) && refusedOwned[2] == large.depthBuffer && ReadCallerState() == caller,
          "refused temporary image retires its texture and preserves the shared target");
  CheckTarget(inspect(renderer), 512);
  UGlTextureOwner unique128(renderer.RenderUnique(ContentKind::Block, blockName, 128, 45.f, 32.f));
  const auto latest = inspect(renderer);
  CheckTarget(latest, 128);
  // The driver may reuse the retired color name for the next unique image.
  const bool oldColorRetired = large.colorTexture == unique128.Get() || !glIsTexture(large.colorTexture);
  Require(unique128.Get() && glIsTexture(unique128.Get()) && unique128.Get() != latest.colorTexture &&
              oldColorRetired && !glIsFramebuffer(large.framebuffer) && !glIsRenderbuffer(large.depthBuffer) &&
              ReadCallerState() == caller,
          "shrinking for a unique draw retires the old shared target");

  const auto shader = shaders->GetShader("gui_prefab_icon");
  renderer.Shutdown();
  renderer.Shutdown();
  const auto ended = inspect(renderer);
  Require(ended.supported && !ended.vertexBytes && !ended.indexBytes && !ended.targetSize && !ended.shaderPresent &&
              !glIsVertexArray(mesh.vertexArray) && !glIsBuffer(mesh.vertexBuffer) && !glIsBuffer(mesh.indexBuffer) &&
              TargetIsRetired(latest) && glIsTexture(unique64.Get()) && glIsTexture(unique128.Get()) &&
              glIsTexture(sourceTexture) && shader && glIsProgram(shader->GetProgramID()) && ReadCallerState() == caller &&
              glGetError() == GL_NO_ERROR, "shutdown retires owned GL names and preserves external outputs, sources and caller state");

  glBindVertexArray(GLuint(original.vao));
  glBindBuffer(GL_ARRAY_BUFFER, GLuint(original.array));
  glBindBuffer(GL_PIXEL_UNPACK_BUFFER, GLuint(original.unpack));
  glBindRenderbuffer(GL_RENDERBUFFER, GLuint(original.renderbuffer));
  glDeleteVertexArrays(1, &callerVao);
  glDeleteBuffers(GLsizei(callerBuffers.size()), callerBuffers.data());
  glDeleteRenderbuffers(1, &callerRenderbuffer);
  glActiveTexture(GL_TEXTURE2);
  glBindTexture(GL_TEXTURE_2D, GLuint(original.texture2));
  glClearColor(original.clear[0], original.clear[1], original.clear[2], original.clear[3]);
  UObjectLibrary::ReleaseThreadRead();
  textures->Clear();
  return {{"status", "PASS"}, {"renderer_bytes", shell}, {"vertex_bytes", mesh.vertexBytes},
          {"index_bytes", mesh.indexBytes}, {"target_sizes", {64, 512, 128}}, {"depth_bits", 24},
          {"mesh_refusal_retried", true}, {"resize_refusal_preserves_target", true},
          {"temporary_texture_refusal_retired", true}, {"both_render_paths_restore_caller", true},
          {"normal_retirement_and_borrowed_sources", true}};
}
} // namespace cutum::shared_content_preview_fixture
