#pragma once
#include "App/Application.h"
#include "App/Core.h"
#include "Blocks/BlockDefinitionStorage.h"
#include "Gui/Widgets/GuiButton.h"
#include "Gui/Widgets/GuiLabel.h"
#include "Render/Pipeline/GlStateScope.h"
#include "ThirdParty/stb_image_write.h"
#include "World/IO/BinaryChunkSerializer.h"
#include "World/IO/ChunkStorageService.h"
#include "World/IO/TerrainSliceLoad.h"
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>
#include <thread>

namespace cutum
{
// Explicit native QA only. Preserve the running application's context and use
// a fresh world plus a separate GUI context, with the real update/render/input.
inline void RunSaveErrorUiProbe(UApplication &application, UCore &core)
{
  const char *output = std::getenv("CUBA_FLIGHT_QA_TERRAIN_SAVE");
  if (!output || !*output)
    return;
  namespace fs = std::filesystem;
  using Json = nlohmann::json;
  const auto root = fs::absolute(output) / "save-error-ui";
  if (fs::exists(root))
    throw std::runtime_error("Save UI probe refuses existing output");
  fs::create_directories(root);
  Json report = {{"schema", "save-error-ui-native.v1"}, {"status", "FAIL"}};
  auto require = [](bool value, const char *message)
  {
    if (!value)
      throw std::runtime_error(message);
  };
  const auto previous_world = application.World;
  const auto previous_core_world = core.WorldInstance;
  const auto previous_folder = core.ActiveWorldFolder;
  const auto previous_merge = core.BlockMergeRegistryInstance;
  const auto previous_state = application.State;
  const auto previous_session = application.WorldSessionActive;
  const auto previous_console = application.ConsoleOpen;
  const auto previous_palette = application.PaletteOpen;
  const auto previous_cursor = application.FreeCursor;
  const auto previous_suppress = application.SuppressConsoleToggleChar;
  const auto previous_menu = application.MainMenuScreen;
  const auto previous_progress = application.ProgressScreen;
  const auto previous_subview = application.MenuSubview;
  const auto previous_quit = application.QuitRequested;
  const auto previous_sink = application.ProgressSink;
  auto previous_gui = std::move(application.GuiContext);
  auto previous_runner = std::move(application.WorldOpRunner);
  auto previous_complete = std::move(application.WorldOpOnComplete);
  auto previous_deferred = std::move(application.PendingMenuAction);
  const int previous_close =
      application.Window ? glfwWindowShouldClose(application.Window) : 0;
  struct Restore
  {
    std::function<void()> action;
    ~Restore() { action(); }
  } restore{[&]()
            {
              application.PendingMenuAction = std::move(previous_deferred);
              application.WorldOpOnComplete = std::move(previous_complete);
              application.WorldOpRunner = std::move(previous_runner);
              application.GuiContext = std::move(previous_gui);
              application.World = previous_world;
              core.WorldInstance = previous_core_world;
              core.ActiveWorldFolder = previous_folder;
              core.BlockMergeRegistryInstance = previous_merge;
              application.State = previous_state;
              application.WorldSessionActive = previous_session;
              application.ConsoleOpen = previous_console;
              application.PaletteOpen = previous_palette;
              application.FreeCursor = previous_cursor;
              application.SuppressConsoleToggleChar = previous_suppress;
              application.MainMenuScreen = previous_menu;
              application.ProgressScreen = previous_progress;
              application.MenuSubview = previous_subview;
              application.QuitRequested = previous_quit;
              application.ProgressSink = previous_sink;
              if (application.Window)
                glfwSetWindowShouldClose(application.Window, previous_close);
              application.SyncCursorVisibility();
            }};
  try
  {
    auto world = std::make_shared<UWorld>(nullptr, nullptr);
    world->ProceduralTemplate.MinHeight = -64;
    world->ProceduralTemplate.MaxHeight = -17;
    auto definitions = std::make_shared<UBlockDefinitionStorage>();
    BlockDefinition stone;
    stone.Physics = BlockPhysicsProfile::Solid();
    definitions->ReplaceAll({{8, stone}}, {{"stone", 8}});
    world->BlockRegistry =
        std::make_unique<UBlockRegistry>(nullptr, definitions);
    world->SetWorldFolderPath(root.string());
    UBinaryChunkSerializer codec;
    const glm::ivec3 column(0), floor(0, -4, 0), cell(1, -62, 3);
    for (int cy = -4; cy <= -2; ++cy)
    {
      glm::ivec3 coord(0, cy, 0);
      UChunk frame(coord);
      if (cy == -4)
        frame.SetBlockLocal({1, 2, 3}, 8);
      auto decoded = codec.Deserialize(
          codec.Serialize(coord, frame, *world->BlockRegistry).bytes, coord,
          *world->BlockRegistry);
      require(ApplyTerrainSliceLoad(world->BlockWorld, coord, &decoded, nullptr,
                                    {column, 1}, 1, false),
              "save UI fixture decode");
    }
    auto &storage = world->GetChunkStorage();
    require(storage.SaveTerrainColumn(column, world->BlockWorld, root.string(),
                                      *world->BlockRegistry, -17, -64),
            "save UI fixture disk base");
    world->BlockWorld.SetBlock(cell, BLOCK_AIR);
    world->ModifiedChunks.insert(floor);
    const fs::path blocker =
        UChunkStorageService::ColumnLightFilePath(root.string(), column) +
        ".tmp";
    require(fs::create_directory(blocker), "save UI owns write blocker");
    std::ofstream(blocker / "owned-blocker") << "save UI fixture";
    application.World = world;
    core.WorldInstance = world;
    core.ActiveWorldFolder = root;
    core.BlockMergeRegistryInstance.reset();
    application.GuiContext = std::make_unique<UGuiContext>();
    require(application.GuiContext->Initialize(application.ShaderManager,
                                               application.TextRenderer),
            "save UI context initializes");
    application.WorldSessionActive = true;
    application.MainMenuScreen = nullptr;
    application.ProgressScreen = nullptr;
    application.State = AppState::MainMenu;
    bool close_callback = false;
    WorldRunnerRequest request;
    request.op = WorldRunnerOp::Shutdown;
    request.shutdownSaveSession = true;
    request.shutdownCloseApplication = true;
    request.saveConfigAfter = false;
    application.BeginWorldOperation(request, [&]() { close_callback = true; });
    report["loading_close_intercepted"] =
        application.TryBeginShutdownFromWindowClose();
    report["loading_close_requested_quit"] = application.QuitRequested;
    auto finish = [&]()
    {
      const auto deadline =
          std::chrono::steady_clock::now() + std::chrono::seconds(12);
      while (application.WorldOpRunner && application.WorldOpRunner->IsActive())
      {
        require(std::chrono::steady_clock::now() < deadline,
                "save UI operation bound");
        application.Update(0.0);
        std::this_thread::yield();
      }
    };
    finish();
    report["error"] = application.WorldOpRunner->ErrorMessage();
    report["close_callback_called"] = close_callback;
    report["failure_screen_retained"] =
        application.ProgressScreen &&
        application.GuiContext->GetScreen() == application.ProgressScreen;
    auto capture = [&](const char *name, int width, int height)
    {
      UGlStateScope state(kGlMaskFpViewmodel3D);
      GLuint fbo = 0, texture = 0;
      glGenFramebuffers(1, &fbo);
      glGenTextures(1, &texture);
      glBindFramebuffer(GL_FRAMEBUFFER, fbo);
      glBindTexture(GL_TEXTURE_2D, texture);
      glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA,
                   GL_UNSIGNED_BYTE, nullptr);
      glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                             GL_TEXTURE_2D, texture, 0);
      require(glCheckFramebufferStatus(GL_FRAMEBUFFER) ==
                  GL_FRAMEBUFFER_COMPLETE,
              "save UI capture FBO");
      application.RenderFrame(width, height, 0.0);
      std::vector<unsigned char> pixels(size_t(width) * height * 4);
      glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE,
                   pixels.data());
      const size_t stride = size_t(width) * 4;
      for (int y = 0; y < height / 2; ++y)
        std::swap_ranges(pixels.begin() + y * stride,
                         pixels.begin() + (y + 1) * stride,
                         pixels.begin() + (height - y - 1) * stride);
      const bool saved =
          stbi_write_png((root / name).string().c_str(), width, height, 4,
                         pixels.data(), width * 4) != 0;
      glDeleteFramebuffers(1, &fbo);
      glDeleteTextures(1, &texture);
      require(saved, "save UI screenshot written");
    };
    capture("save-failure-1280.png", 1280, 720);
    require(report["failure_screen_retained"].get<bool>(),
            "save error must remain visible instead of disappearing into main "
            "menu");
    require(report["loading_close_intercepted"].get<bool>() &&
                !application.QuitRequested,
            "window close during save cannot bypass save completion");
    require(!close_callback && world->ModifiedChunks.count(floor) &&
                world->BlockWorld.GetBlock(cell) == BLOCK_AIR,
            "failed save UI retains session and suppresses close callback");
    application.Update(0.0);
    capture("save-failure-640.png", 640, 480);
    std::function<UGuiButton *(UGuiWidget *)> find_retry =
        [&](UGuiWidget *widget) -> UGuiButton *
    {
      if (auto *button = dynamic_cast<UGuiButton *>(widget);
          button && button->GetLabel() == "Retry save")
        return button;
      for (const auto &child : widget->GetChildren())
        if (auto *found = find_retry(child.get()))
          return found;
      return nullptr;
    };
    auto *retry = find_retry(application.GuiContext->GetScreen()->GetRoot());
    require(retry && retry->IsVisible() && retry->IsEnabled(),
            "save UI exposes retry button");
    require(fs::remove(blocker / "owned-blocker") && fs::remove(blocker),
            "save UI removes only owned blocker");
    const auto bounds = retry->GetBounds();
    GuiMouseEvent click;
    click.X = bounds.X + bounds.W / 2;
    click.Y = bounds.Y + bounds.H / 2;
    click.Pressed = true;
    application.GuiContext->RouteMouseDown(click);
    click.Pressed = false;
    application.GuiContext->RouteMouseUp(click);
    require(bool(application.PendingMenuAction),
            "retry click schedules action before screen destruction");
    application.Update(0.0);
    finish();
    UBlockWorld cold;
    require(
        application.WorldOpRunner->Succeeded() && !application.QuitRequested &&
            !close_callback && application.WorldSessionActive &&
            world->ModifiedChunks.empty() &&
            storage.LoadTerrainColumn(column, cold, root.string(),
                                      *world->BlockRegistry, -17, -64) > 0 &&
            cold.GetBlock(cell) == BLOCK_AIR,
        "clicked retry persists edit without closing session");
    report["retry_via_mouse"] = true;
    report["status"] = "PASS";
  }
  catch (const std::exception &error)
  {
    report["failure"] = error.what();
  }
  std::ofstream(root / "report.json") << report.dump(2) << '\n';
}
} // namespace cutum
