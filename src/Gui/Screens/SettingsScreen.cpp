#include "SettingsScreen.h"
#include "App/Settings/AppSettingsSnapshot.h"
#include "Gui/Core/GuiContext.h"
#include "Gui/Core/GuiRenderer.h"
#include "Gui/Interfaces/IGuiMenuHost.h"
#include "Gui/Layout/GuiLayout.h"
#include "Gui/Widgets/GuiButton.h"
#include "Gui/Widgets/GuiCheckbox.h"
#include "Gui/Widgets/GuiDialogFrame.h"
#include "Gui/Widgets/GuiLabel.h"
#include "Gui/Widgets/GuiPanel.h"
#include "Gui/Widgets/GuiTextInput.h"
#include "Gui/Widgets/GuiWindow.h"
#include "Gui/Widgets/WorldGenSettingsForm.h"
#include "App/Settings/UiSettings.h"
#include <algorithm>

namespace cutum
{

namespace
{

int ParseIntOr(const std::string &text, int fallback)
{
  try
  {
    return std::stoi(text);
  }
  catch (...)
  {
    return fallback;
  }
}

GuiGridSpec BuildTwoColumnSpec(int width)
{
  GuiGridSpec spec;
  spec.columns = width < 720 ? 1 : 2;
  spec.hGap = 12;
  spec.vGap = 8;
  spec.padding = 4;
  spec.columnWeights = {1, 1};
  return spec;
}

} // namespace

USettingsScreen::USettingsScreen(IGuiMenuHost *host) : host_(host) {}

void USettingsScreen::OnSave()
{
  if (!host_)
  {
    return;
  }
  AppSettingsSnapshot app = host_->LoadAppSettingsSnapshot();
  if (defaultUserInput_)
  {
    app.DefaultUser = defaultUserInput_->GetText();
  }
  if (defaultWorldInput_)
  {
    app.DefaultWorld = defaultWorldInput_->GetText();
  }
  if (renderDistInput_)
  {
    app.RenderDistanceChunks =
        ParseIntOr(renderDistInput_->GetText(), app.RenderDistanceChunks);
  }
  if (streamingBox_)
  {
    app.StreamingEnabled = streamingBox_->IsChecked();
  }
  if (stepUpBox_)
  {
    app.StepUpEnabled = stepUpBox_->IsChecked();
  }
  if (greedyBox_)
  {
    app.Render.greedyMeshing = greedyBox_->IsChecked();
  }
  if (faceQuadsBox_)
  {
    app.Render.faceQuads = faceQuadsBox_->IsChecked();
  }
  if (frustumBox_)
  {
    app.Render.frustumCulling = frustumBox_->IsChecked();
  }
  if (batchCacheBox_)
  {
    app.Render.batchCache = batchCacheBox_->IsChecked();
  }
  if (legacyHudBox_)
  {
    app.Ui.legacyHud = legacyHudBox_->IsChecked();
  }
  if (consoleKeyInput_)
  {
    app.Ui.consoleKey = consoleKeyInput_->GetText();
  }
  if (paletteKeyInput_)
  {
    app.Ui.paletteKey = paletteKeyInput_->GetText();
  }
  app.Ui.hotbarCount = std::clamp(hotbarCount_, 1, 2);
  app.Ui.controlScheme = controlScheme_;
  if (app.Render.greedyMeshing && !app.Render.faceQuads)
  {
    app.Render.faceQuads = true;
  }

  ProceduralSettings proc =
      worldForm_ ? worldForm_->ReadSettings() : host_->LoadProceduralTemplate();
  host_->SaveAppAndTemplateSettings(app, proc);
  host_->ReturnToMainMenu();
}

void USettingsScreen::ShowTab(int tab)
{
  if (dialogFrame_)
  {
    dialogFrame_->SetActivePage(tab);
  }
}

void USettingsScreen::Build(UGuiContext &ctx)
{
  int w = ctx.GetRenderer().GetWindowWidth();
  int h = ctx.GetRenderer().GetWindowHeight();
  if (w > 0 && h > 0)
  {
    viewportW_ = w;
    viewportH_ = h;
  }

  const GuiTheme &theme = ctx.GetTheme();
  const AppSettingsSnapshot appSnap =
      host_ ? host_->LoadAppSettingsSnapshot() : AppSettingsSnapshot{};
  const ProceduralSettings procSnap =
      host_ ? host_->LoadProceduralTemplate() : ProceduralSettings{};
  hotbarCount_ = std::clamp(appSnap.Ui.hotbarCount, 1, 2);
  controlScheme_ = appSnap.Ui.controlScheme;

  auto backdrop = std::make_unique<UGuiPanel>(&theme);
  backdrop->SetBounds({0, 0, viewportW_, viewportH_});

  const int winW = std::min(860, viewportW_ - 32);
  const int winH = std::min(540, viewportH_ - 32);
  auto window = std::make_unique<UGuiWindow>(&theme, "Settings");
  Window = window.get();
  window->SetBounds(
      {(viewportW_ - winW) / 2, (viewportH_ - winH) / 2, winW, winH});

  auto frame = std::make_unique<UGuiDialogFrame>(&theme);
  dialogFrame_ = frame.get();
  frame->SetScrollbarMode(GuiScrollbarMode::Hidden);
  frame->CreateTabBar({"Application", "World defaults"},
                      [this](int tab) { ShowTab(tab); });

  UGuiPanel &app = frame->AddScrollPage();
  appPanel_ = &app;
  auto defaultUserLbl = std::make_unique<UGuiLabel>(&theme, "Default user:");
  defaultUserLabel_ = defaultUserLbl.get();
  app.AddChild(std::move(defaultUserLbl));
  auto userIn = std::make_unique<UGuiTextInput>(&theme);
  defaultUserInput_ = userIn.get();
  userIn->SetText(appSnap.DefaultUser);
  app.AddChild(std::move(userIn));
  auto defaultWorldLbl =
      std::make_unique<UGuiLabel>(&theme, "Default world folder:");
  defaultWorldLabel_ = defaultWorldLbl.get();
  app.AddChild(std::move(defaultWorldLbl));
  auto worldIn = std::make_unique<UGuiTextInput>(&theme);
  defaultWorldInput_ = worldIn.get();
  worldIn->SetText(appSnap.DefaultWorld);
  app.AddChild(std::move(worldIn));
  auto renderDistLbl =
      std::make_unique<UGuiLabel>(&theme, "Render distance (chunks):");
  renderDistLabel_ = renderDistLbl.get();
  app.AddChild(std::move(renderDistLbl));
  auto distIn = std::make_unique<UGuiTextInput>(&theme);
  renderDistInput_ = distIn.get();
  distIn->SetText(std::to_string(appSnap.RenderDistanceChunks));
  app.AddChild(std::move(distIn));
  auto stream = std::make_unique<UGuiCheckbox>(&theme, "Streaming enabled");
  streamingBox_ = stream.get();
  stream->SetChecked(appSnap.StreamingEnabled);
  app.AddChild(std::move(stream));
  auto step = std::make_unique<UGuiCheckbox>(&theme, "Step up");
  stepUpBox_ = step.get();
  step->SetChecked(appSnap.StepUpEnabled);
  app.AddChild(std::move(step));
  auto greedy = std::make_unique<UGuiCheckbox>(&theme, "Greedy meshing");
  greedyBox_ = greedy.get();
  greedy->SetChecked(appSnap.Render.greedyMeshing);
  app.AddChild(std::move(greedy));
  auto face = std::make_unique<UGuiCheckbox>(&theme, "Face quads");
  faceQuadsBox_ = face.get();
  face->SetChecked(appSnap.Render.faceQuads);
  app.AddChild(std::move(face));
  auto frust = std::make_unique<UGuiCheckbox>(&theme, "Frustum culling");
  frustumBox_ = frust.get();
  frust->SetChecked(appSnap.Render.frustumCulling);
  app.AddChild(std::move(frust));
  auto batch = std::make_unique<UGuiCheckbox>(&theme, "Batch cache");
  batchCacheBox_ = batch.get();
  batch->SetChecked(appSnap.Render.batchCache);
  app.AddChild(std::move(batch));
  auto hud = std::make_unique<UGuiCheckbox>(&theme, "Legacy HUD");
  legacyHudBox_ = hud.get();
  hud->SetChecked(appSnap.Ui.legacyHud);
  app.AddChild(std::move(hud));
  auto consoleLbl = std::make_unique<UGuiLabel>(&theme, "Console key:");
  consoleKeyLabel_ = consoleLbl.get();
  app.AddChild(std::move(consoleLbl));
  auto ckIn = std::make_unique<UGuiTextInput>(&theme);
  consoleKeyInput_ = ckIn.get();
  ckIn->SetText(appSnap.Ui.consoleKey);
  app.AddChild(std::move(ckIn));
  auto paletteLbl = std::make_unique<UGuiLabel>(&theme, "Palette key:");
  paletteKeyLabel_ = paletteLbl.get();
  app.AddChild(std::move(paletteLbl));
  auto pkIn = std::make_unique<UGuiTextInput>(&theme);
  paletteKeyInput_ = pkIn.get();
  pkIn->SetText(appSnap.Ui.paletteKey);
  app.AddChild(std::move(pkIn));
  auto hotbarLbl = std::make_unique<UGuiLabel>(&theme, "Hotbar count:");
  hotbarCountLabel_ = hotbarLbl.get();
  app.AddChild(std::move(hotbarLbl));
  auto hotbarValue =
      std::make_unique<UGuiLabel>(&theme, std::to_string(hotbarCount_));
  hotbarValue->SetTextAlign(GuiTextAlign::Center);
  hotbarCountValueLabel_ = hotbarValue.get();
  app.AddChild(std::move(hotbarValue));
  auto hotbarMinus = std::make_unique<UGuiButton>(&theme, "-");
  hotbarMinus->SetOnClick(
      [this]()
      {
        hotbarCount_ = std::max(1, hotbarCount_ - 1);
        if (hotbarCountValueLabel_)
        {
          hotbarCountValueLabel_->SetText(std::to_string(hotbarCount_));
        }
      });
  hotbarMinusButton_ = hotbarMinus.get();
  app.AddChild(std::move(hotbarMinus));
  auto hotbarPlus = std::make_unique<UGuiButton>(&theme, "+");
  hotbarPlus->SetOnClick(
      [this]()
      {
        hotbarCount_ = std::min(2, hotbarCount_ + 1);
        if (hotbarCountValueLabel_)
        {
          hotbarCountValueLabel_->SetText(std::to_string(hotbarCount_));
        }
      });
  hotbarPlusButton_ = hotbarPlus.get();
  app.AddChild(std::move(hotbarPlus));

  auto controlSchemeLbl =
      std::make_unique<UGuiLabel>(&theme, "Control scheme:");
  controlSchemeLabel_ = controlSchemeLbl.get();
  app.AddChild(std::move(controlSchemeLbl));
  auto profileBtn = std::make_unique<UGuiButton>(
      &theme,
      controlScheme_ == ControlScheme::Cubatarium ? "Cubatarium" : "Classic");
  profileBtn->SetOnClick(
      [this]()
      {
        controlScheme_ = controlScheme_ == ControlScheme::Classic
                             ? ControlScheme::Cubatarium
                             : ControlScheme::Classic;
        if (controlSchemeButton_)
        {
          controlSchemeButton_->SetLabel(
              controlScheme_ == ControlScheme::Cubatarium ? "Cubatarium"
                                                          : "Classic");
        }
      });
  controlSchemeButton_ = profileBtn.get();
  app.AddChild(std::move(profileBtn));

  UGuiPanel &world = frame->AddScrollPage();
  worldPanel_ = &world;
  worldForm_ = std::make_unique<UWorldGenSettingsForm>(&theme);
  worldForm_->SetSettings(procSnap);
  worldForm_->BuildInto(world);
  frame->SetScrollPageLayout(
      0, [this](const GuiRect &area) { return MeasureAppPageHeight(area); },
      [this](const GuiRect &area) { LayoutAppPage(area); });
  frame->SetScrollPageLayout(
      1, [this](const GuiRect &area) { return MeasureWorldPageHeight(area); },
      [this](const GuiRect &area) { LayoutWorldPage(area); });

  auto saveBtn = std::make_unique<UGuiButton>(&theme, "Save");
  saveBtn->SetOnClick([this]() { OnSave(); });
  frame->AddFooterButton(std::move(saveBtn));
  auto cancelBtn = std::make_unique<UGuiButton>(&theme, "Cancel");
  cancelBtn->SetOnClick(
      [this]()
      {
        if (host_)
        {
          host_->ReturnToMainMenu();
        }
      });
  frame->AddFooterButton(std::move(cancelBtn));

  window->AddChild(std::move(frame));
  backdrop->AddChild(std::move(window));
  root_ = std::move(backdrop);

  Relayout();
  ShowTab(0);
}

void USettingsScreen::OnViewportChanged(int width, int height)
{
  UGuiScreenBase::OnViewportChanged(width, height);
  Relayout();
}

void USettingsScreen::Relayout()
{
  if (!Window || !dialogFrame_)
  {
    return;
  }
  const int winW = std::min(860, viewportW_ - 32);
  const int winH = std::min(540, viewportH_ - 32);
  Window->SetBounds(
      {(viewportW_ - winW) / 2, (viewportH_ - winH) / 2, winW, winH});
  dialogFrame_->SetBounds(Window->GetClientArea());
  dialogFrame_->LayoutFrame();
}

std::vector<GuiGridItem>
USettingsScreen::BuildAppGridItems(const GuiGridSpec &spec) const
{
  const int hotbarValueRow = spec.columns > 1 ? 9 : 10;
  const int hotbarValueCol = spec.columns > 1 ? 1 : 0;
  return {
      {defaultUserLabel_, 0, 0, 1, 1, 28},
      {defaultUserInput_, 0, 1, 1, 1, 32},
      {defaultWorldLabel_, 1, 0, 1, 1, 28},
      {defaultWorldInput_, 1, 1, 1, 1, 32},
      {renderDistLabel_, 2, 0, 1, 1, 28},
      {renderDistInput_, 2, 1, 1, 1, 32},
      {streamingBox_, 3, 0, 1, 1, 30},
      {stepUpBox_, 3, 1, 1, 1, 30},
      {greedyBox_, 4, 0, 1, 1, 30},
      {faceQuadsBox_, 4, 1, 1, 1, 30},
      {frustumBox_, 5, 0, 1, 1, 30},
      {batchCacheBox_, 5, 1, 1, 1, 30},
      {legacyHudBox_, 6, 0, 1, 2, 30},
      {consoleKeyLabel_, 7, 0, 1, 1, 28},
      {consoleKeyInput_, 7, 1, 1, 1, 32},
      {paletteKeyLabel_, 8, 0, 1, 1, 28},
      {paletteKeyInput_, 8, 1, 1, 1, 32},
      {hotbarCountLabel_, 9, 0, 1, 1, 28},
      {hotbarCountValueLabel_, hotbarValueRow, hotbarValueCol, 1, 1, 32},
      {controlSchemeLabel_, 10, 0, 1, 1, 28},
      {controlSchemeButton_, 10, 1, 1, 1, 32},
  };
}

void USettingsScreen::LayoutHotbarCountControls(const GuiGridSpec &spec) const
{
  if (!hotbarCountValueLabel_ || !hotbarMinusButton_ || !hotbarPlusButton_)
  {
    return;
  }

  constexpr int btnSize = 32;
  constexpr int valueW = 28;
  constexpr int gap = 6;

  if (spec.columns <= 1)
  {
    const GuiRect anchor = hotbarCountValueLabel_->GetBounds();
    const int y = anchor.y + (anchor.h - btnSize) / 2;
    const int x = anchor.x;
    hotbarCountValueLabel_->SetBounds({x, y, valueW, btnSize});
    hotbarPlusButton_->SetBounds({x + valueW + gap, y, btnSize, btnSize});
    hotbarMinusButton_->SetBounds(
        {x + valueW + gap + btnSize + gap, y, btnSize, btnSize});
    return;
  }

  const GuiRect cell = hotbarCountValueLabel_->GetBounds();
  const int y = cell.y + (cell.h - btnSize) / 2;
  const int startX = cell.x;
  hotbarCountValueLabel_->SetBounds({startX, y, valueW, btnSize});
  hotbarPlusButton_->SetBounds({startX + valueW + gap, y, btnSize, btnSize});
  hotbarMinusButton_->SetBounds(
      {startX + valueW + gap + btnSize + gap, y, btnSize, btnSize});
}

int USettingsScreen::MeasureAppPageHeight(const GuiRect &area) const
{
  const GuiGridSpec spec = BuildTwoColumnSpec(area.w);
  return UGuiLayout::GridMeasure(area, spec, BuildAppGridItems(spec));
}

void USettingsScreen::LayoutAppPage(const GuiRect &area) const
{
  const GuiGridSpec spec = BuildTwoColumnSpec(area.w);
  UGuiLayout::GridPlace(area, spec, BuildAppGridItems(spec));
  LayoutHotbarCountControls(spec);
}

int USettingsScreen::MeasureWorldPageHeight(const GuiRect &area) const
{
  if (!worldForm_)
  {
    return 0;
  }
  return worldForm_->MeasureGridHeight(area, BuildTwoColumnSpec(area.w));
}

void USettingsScreen::LayoutWorldPage(const GuiRect &area) const
{
  if (!worldForm_)
  {
    return;
  }
  worldForm_->LayoutGrid(area, BuildTwoColumnSpec(area.w));
}

} // namespace cutum
