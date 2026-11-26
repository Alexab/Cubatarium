#pragma once
#include "Test/GuiContextStorageFixture.h"
#include "App/Application.h"
#include "App/Core.h"
#include "Blocks/BlockDefinitionStorage.h"
#include "Gui/Widgets/GuiButton.h"
#include "Gui/Widgets/GuiLabel.h"
#include "Render/Pipeline/GlStateScope.h"
#include "ThirdParty/stb_image_write.h"
#include "Test/ApplicationOperationStorageFixture.h"
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
    report["application_operation_model"]=RunApplicationOperationStorageProbe(application,core,*application.GetWorldRootForStorage());
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
    auto progress_storage = [&]() {
      const auto *screen = dynamic_cast<const UWorldProgressScreen *>(application.GuiContext->GetScreen());
      require(screen && screen == application.ProgressScreen, "native application progress binding");
      const auto source = screen->InspectOwnedStorage(*application.GuiContext, &application);
      require(source.supported, "native progress screen has a supported widget and callback graph");
      return WorldProgressStorageJson(source);
    };
    report["progress_storage_loading"] = progress_storage();
    {
      AllocatorSourcePhase phase(NewRootStorageEpoch(),1);const auto source=application.InspectOperationStorage(phase,true);
      report["unknown_completion_source"]=ApplicationOperationSourceJson(source);
      require(!source.supported && source.runner.supported && !source.actions[1].Supported(),"unknown external completion rejects while actual runner source remains supported");phase.Close();
    }
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
    report["progress_storage_failure"] = progress_storage();
    std::function<UGuiButton *(UGuiWidget *, const char *)> find_button =
        [&](UGuiWidget *widget, const char *label) -> UGuiButton *
    {
      if (auto *button = dynamic_cast<UGuiButton *>(widget);
          button && button->GetLabel() == label)
        return button;
      for (const auto &child : widget->GetChildren())
        if (auto *found = find_button(child.get(), label))
          return found;
      return nullptr;
    };
    auto *retry = find_button(application.GuiContext->GetScreen()->GetRoot(), "Retry save");
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
    {
      AllocatorSourcePhase phase(NewRootStorageEpoch(),1);const auto source=application.InspectOperationStorage(phase,true);
      report["pending_retry_source"]=ApplicationOperationSourceJson(source);
      require(source.supported && source.actions[0].kind==ApplicationActionKind::RetrySave,"actual mouse-scheduled retry source");phase.Close();
    }
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
    require(!application.ProgressScreen &&
                !dynamic_cast<const UWorldProgressScreen *>(application.GuiContext->GetScreen()),
            "successful retry retires the active progress screen");
    report["progress_storage_retired"] = true;
    auto &gui = *application.GuiContext;
    const auto paused = application.InspectMainMenuStorage();
    require(paused.supported && paused.present && paused.resume, "paused menu after save retry");
    report["main_menu_storage_paused"] = MainMenuStorageJson(paused);
    gui.NotifyViewport(1280, 720);
    const auto click_label = [&](const char *label)
    {
      auto *button = find_button(gui.GetScreen()->GetRoot(), label);
      require(button && button->IsEnabled() && button->IsVisible(), "native main menu button is active");
      const auto &rect = button->GetBounds();
      GuiMouseEvent event;
      event.X = rect.X + rect.W / 2;
      event.Y = rect.Y + rect.H / 2;
      event.Pressed = true;
      require(gui.RouteMouseDown(event), "native menu captures button press");
      event.Pressed = false;
      require(gui.RouteMouseUp(event), "native menu consumes button release");
    };
    click_label("Quit");
    const auto modal = application.InspectMainMenuStorage();
    require(modal.supported && modal.quit_dialog && modal.requested == paused.requested,
            "native quit confirmation reuses the paused menu ownership");
    report["main_menu_storage_quit_dialog"] = MainMenuStorageJson(modal);
    click_label("No");
    require(!application.InspectMainMenuStorage().quit_dialog && !application.QuitRequested,
            "native menu cancels quit");
    click_label("Resume");
    const auto resumed = application.InspectMainMenuStorage();
    require(resumed.supported && !resumed.present && application.State == AppState::InGame,
            "native resume detaches and retires the menu and its application alias");
    report["main_menu_storage_resumed"] = MainMenuStorageJson(resumed);
    application.State = AppState::InGame;
    application.ShowFatalSaveFailure(
        "The save disk is full. Your latest changes remain in memory. "
        "Free disk space before saving again.");
    require(application.State == AppState::MainMenu &&
                application.World == world && application.WorldSessionActive &&
                !application.QuitRequested && application.ProgressScreen &&
                application.GuiContext->GetScreen() == application.ProgressScreen,
            "fatal autosave screen retains the world and leaves gameplay");
    require(application.WantsCaptureMouse(),
            "fatal save menu captures mouse input");
    capture("disk-full-menu-1280.png", 1280, 720);
    capture("disk-full-menu-640.png", 640, 480);
    report["gui_context_storage"] = GuiContextStorageJson(application.GuiContext->InspectOwnedStorage());
    report["progress_storage_fatal"] = progress_storage();
    report["gui_context_lifecycle"] = gui_context_fixture::RunLifecycle();
    report["fatal_save_menu"] = "PASS";
    report["fatal_save_scope"] =
        "Direct application fatal-save handler; no disk-full injection or "
        "WindowManager autosave scheduler acceptance";
    report["status"] = "PASS";
  }
  catch (const std::exception &error)
  {
    report["failure"] = error.what();
  }
  std::ofstream(root / "report.json") << report.dump(2) << '\n';
}
} // namespace cutum
