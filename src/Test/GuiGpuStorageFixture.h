#pragma once

#include "App/Application.h"
#include "App/Platform/GameAssets.h"
#include "Render/Engine/RenderImageStorageReport.h"
#include "Render/Engine/TerrainGpuBufferReport.h"
#include "Render/Engine/TextRenderer.h"
#include "Test/GuiContextStorageFixture.h"
#include "Test/OrdinaryNewObservation.h"
#include <filesystem>

namespace cutum::gui_gpu_storage_fixture
{
inline nlohmann::json Run()
{
  using gui_context_fixture::Require;
  namespace fs = std::filesystem;
  auto directory = fs::current_path();
  while (!fs::is_regular_file(directory / kBundledUiFontRelPath) && directory.has_relative_path())
    directory = directory.parent_path();
  const auto font = (directory / kBundledUiFontRelPath).string();
  const auto missingFont = font + "/missing.ttf";
  Require(fs::is_regular_file(font), "GUI GPU fixture uses the bundled font");

  auto text = std::make_shared<UTextRenderer>();
  auto shaders = std::make_shared<UShaderManager>();
  UApplication application({}, {}, {}, {}, text, shaders, {});
  auto &gui = application.GetGui();
  const auto buffers = [&] {
    TerrainGpuBufferCensus out;
    application.InspectGuiGpuBuffers(out);
    return out;
  };
  const auto glyphs = [&] {
    RenderImageCensus out;
    text->InspectRenderImages(out);
    return out;
  };
  const auto bytes = [](const TerrainGpuBufferSnapshot &s, TerrainGpuBufferRole role) {
    return s.roles[static_cast<size_t>(role)].bytes;
  };
  const auto cold = buffers().Read();
  Require(cold.contextAvailable && !cold.references, "cold GUI and text expose no GL buffers");

  GLuint sentinel{};
  glGenBuffers(1, &sentinel);
  glBindBuffer(GL_COPY_READ_BUFFER, sentinel);
  glBufferData(GL_COPY_READ_BUFFER, 24, nullptr, GL_STATIC_DRAW);
  glBindBuffer(GL_PIXEL_UNPACK_BUFFER, sentinel);
  glPixelStorei(GL_UNPACK_ALIGNMENT, 8);
  glPixelStorei(GL_UNPACK_ROW_LENGTH, 33);
  glPixelStorei(GL_UNPACK_SKIP_PIXELS, 2);
  Require(text->Initialize(font, 16), "bundled font publishes with unrelated unpack state");
  GLint unpackBuffer{}, alignment{}, rowLength{}, skipPixels{};
  glGetIntegerv(GL_PIXEL_UNPACK_BUFFER_BINDING, &unpackBuffer);
  glGetIntegerv(GL_UNPACK_ALIGNMENT, &alignment);
  glGetIntegerv(GL_UNPACK_ROW_LENGTH, &rowLength);
  glGetIntegerv(GL_UNPACK_SKIP_PIXELS, &skipPixels);
  Require(static_cast<GLuint>(unpackBuffer) == sentinel && alignment == 8 && rowLength == 33 && skipPixels == 2,
          "glyph upload restores the caller's unpack state");
  glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
  glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
  glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
  glPixelStorei(GL_UNPACK_SKIP_PIXELS, 0);

  Require(gui.Initialize(shaders, text), "actual GUI context brings up colored and textured draws");
  const auto initial = buffers().Read();
  GLint copyReading{};
  glGetIntegerv(GL_COPY_READ_BUFFER_BINDING, &copyReading);
  Require(static_cast<GLuint>(copyReading) == sentinel && !initial.failedQueries && initial.references == 4 &&
              initial.total.objects == 3 && initial.total.bytes == 12480 &&
              bytes(initial, TerrainGpuBufferRole::GuiColored) == 12288 &&
              bytes(initial, TerrainGpuBufferRole::GuiTextured) == 96 &&
              bytes(initial, TerrainGpuBufferRole::TextVertices) == 96,
          "actual initial stores match vertex extents, deduplicate text, and preserve the copy binding");

  const auto oldGlyphCensus = glyphs();
  const auto oldGlyphs = oldGlyphCensus.Read();
  const auto oldSize = text->GetTextSize("World", 1.f);
  RenderImageCensus applicationImages;
  application.InspectRenderImages(applicationImages);
  const auto appGlyphs = applicationImages.Read();
  Require(oldGlyphs.images.size() > 90 && oldGlyphs.images.size() <= 128 && !oldGlyphs.failedQueries &&
              appGlyphs.logicalBytes == oldGlyphs.logicalBytes && appGlyphs.references == oldGlyphs.references,
          "application owns the same measured glyphs as the text renderer");
  for (const auto &glyph : oldGlyphs.images)
    Require(glyph.levels.size() == 1 && glyph.levels[0].bitsPerSample == 8 &&
                glyph.logicalBytes == static_cast<uint64_t>(glyph.levels[0].width) * glyph.levels[0].height,
            "each real glyph has one eight-bit level");

  const GuiRect rect{2, 2, 20, 20};
  auto &renderer = gui.GetRenderer();
  renderer.BeginFrame(32, 32);
  for (int i = 0; i < 300; ++i)
    renderer.DrawFilledRect(rect, glm::vec4(1.f));
  renderer.DrawTexturedRect(rect, oldGlyphs.images.front().name);
  renderer.DrawText("World", 1, 1, glm::vec3(1.f));
  renderer.EndFrame();
  const auto grown = buffers().Read();
  Require(bytes(grown, TerrainGpuBufferRole::GuiColored) == 14400 && grown.total.bytes == 14592,
          "one live flush grows the colored store while the other stores keep their extents");
  renderer.BeginFrame(32, 32);
  renderer.DrawFilledRect(rect, glm::vec4(1.f));
  renderer.EndFrame();
  const auto small = buffers().Read();
  Require(bytes(small, TerrainGpuBufferRole::GuiColored) == 48 && small.total.bytes == 240,
          "a smaller flush replaces the live colored store");

  const auto previousText = [&] {
    TerrainGpuBufferCensus out;
    text->InspectGpuBuffers(out);
    return out;
  }();
  text->RenderText("World", 2, 20, 1.f);
  GLint oldProgram{};
  glGetIntegerv(GL_CURRENT_PROGRAM, &oldProgram);
  Require(oldProgram && !text->Initialize(missingFont, 32) && glIsProgram(static_cast<GLuint>(oldProgram)) &&
              text->GetTextSize("World", 1.f) == oldSize && oldGlyphCensus.Read().logicalBytes == oldGlyphs.logicalBytes,
          "invalid font keeps the original glyphs, program and layout");
  for (size_t prefix : {0u, 32u})
  {
    observation::Scope observed(prefix);
    bool refused = false;
    try { text->Initialize(font, 32); }
    catch (const std::bad_alloc &) { refused = true; }
    Require(refused && !observation::State.live && !observation::State.blocks &&
                text->GetTextSize("World", 1.f) == oldSize,
            "refused text publication retires prepared requests and keeps the active text");
  }
  Require(previousText.Read().total.bytes == 96 && oldGlyphCensus.Read().logicalBytes == oldGlyphs.logicalBytes,
          "failed publications keep the exact old GL objects");
  Require(text->Initialize(font, 32), "text can publish a new font size after failed attempts");
  const auto newGlyphCensus = glyphs();
  const auto newGlyphs = newGlyphCensus.Read();
  const auto retiredText = oldGlyphCensus.Read();
  Require(newGlyphs.logicalBytes > oldGlyphs.logicalBytes && text->GetTextSize("World", 1.f).x > oldSize.x &&
              retiredText.namesWithoutObject == oldGlyphs.images.size() && !retiredText.logicalBytes &&
              previousText.Read().namesWithoutObject == 1 && !glIsProgram(static_cast<GLuint>(oldProgram)),
          "font replacement retires the old glyphs, buffer and active program and uses the new layout");

  const auto beforeGuiShutdown = buffers();
  gui.Shutdown();
  const auto afterGuiShutdown = buffers().Read();
  const auto staleGui = beforeGuiShutdown.Read();
  Require(staleGui.namesWithoutObject == 2 && afterGuiShutdown.references == 1 &&
              afterGuiShutdown.total.objects == 1 && afterGuiShutdown.total.bytes == 96 &&
              newGlyphCensus.Read().logicalBytes == newGlyphs.logicalBytes,
          "GUI shutdown retires both of its buffers and leaves the shared text owner live");
  Require(gui.Initialize(shaders, text) && buffers().Read().total.bytes == initial.total.bytes,
          "GUI restart recreates its initial stores while reusing the supplied text owner");
  gui.Shutdown();
  const auto lastText = buffers();
  text->Shutdown();
  const auto retired = buffers().Read();
  Require(!retired.references && !retired.total.bytes && lastText.Read().namesWithoutObject == 1 &&
              newGlyphCensus.Read().namesWithoutObject == newGlyphs.images.size() && glGetError() == GL_NO_ERROR,
          "text shutdown retires the final buffer and glyph images");
  glDeleteBuffers(1, &sentinel);

  return {{"status", "PASS"}, {"initial", DescribeGuiGpuBuffers(initial)}, {"grown", DescribeGuiGpuBuffers(grown)},
          {"small", DescribeGuiGpuBuffers(small)}, {"after_gui_shutdown", DescribeGuiGpuBuffers(afterGuiShutdown)},
          {"retired", DescribeGuiGpuBuffers(retired)}, {"old_glyphs", oldGlyphs.images.size()},
          {"old_glyph_bytes", oldGlyphs.logicalBytes}, {"new_glyphs", newGlyphs.images.size()},
          {"new_glyph_bytes", newGlyphs.logicalBytes}, {"retired_old_glyphs", retiredText.namesWithoutObject},
          {"failed_font_preserved", true}, {"allocation_refusal_prefixes", {0, 32}},
          {"bindings_restored", true}, {"gui_restart", true}};
}
} // namespace cutum::gui_gpu_storage_fixture
