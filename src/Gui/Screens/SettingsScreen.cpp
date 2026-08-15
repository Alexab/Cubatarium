#include "Gui/Screens/SettingsScreen.h"
#include "App/Settings/AppSettingsSnapshot.h"
#include "App/Settings/GraphicsQualityProfile.h"
#include "App/Settings/UiSettings.h"
#include "ResourcePacks/ResourcePackResolver.h"
#include "Gui/Core/GuiContext.h"
#include "Gui/Core/GuiRenderer.h"
#include "Gui/Core/GuiScale.h"
#include "Gui/Interfaces/IUGuiMenuHost.h"
#include "Gui/Layout/GuiLayout.h"
#include "Gui/Widgets/GuiButton.h"
#include "Gui/Widgets/GuiCheckbox.h"
#include "Gui/Widgets/GuiDialogFrame.h"
#include "Gui/Widgets/GuiLabel.h"
#include "Gui/Widgets/GuiPanel.h"
#include "Gui/Widgets/GuiSlider.h"
#include "Gui/Widgets/GuiTextInput.h"
#include "Gui/Widgets/GuiWindow.h"
#include "Gui/Widgets/WorldGenSettingsForm.h"
#include "Gui/Widgets/ResourcePackPickerForm.h"
#include <algorithm>
#include <cmath>

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

GuiGridSpec BuildTwoColumnSpec(const GuiMetrics &metrics, int width)
{
  GuiGridSpec spec;
  spec.columns = metrics.IsNarrow(width) ? 1 : 2;
  spec.hGap = metrics.Dp(12);
  spec.vGap = metrics.Dp(8);
  spec.Padding = metrics.Dp(4);
  spec.columnWeights = {1, 1};
  return spec;
}

} // namespace

USettingsScreen::USettingsScreen(IUGuiMenuHost *host) : Host(host) {}

USettingsScreen::~USettingsScreen() = default;

void USettingsScreen::OnSave()
{
  if (!Host)
  {
    return;
  }
  AppSettingsSnapshot app = Host->LoadAppSettingsSnapshot();
  if (DefaultUserInput)
  {
    app.DefaultUser = DefaultUserInput->GetText();
  }
  if (DefaultWorldInput)
  {
    app.DefaultWorld = DefaultWorldInput->GetText();
  }
  if (RenderDistInput)
  {
    app.RenderDistanceChunks =
        ParseIntOr(RenderDistInput->GetText(), app.RenderDistanceChunks);
  }
  if (StreamingBox)
  {
    app.StreamingEnabled = StreamingBox->IsChecked();
  }
  if (StepUpBox)
  {
    app.StepUpEnabled = StepUpBox->IsChecked();
  }
  if (FoliageClimbBox)
  {
    app.FoliageClimbEnabled = FoliageClimbBox->IsChecked();
  }
  if (GreedyBox)
  {
    app.Render.GreedyMeshing = GreedyBox->IsChecked();
  }
  if (FaceQuadsBox)
  {
    app.Render.FaceQuads = FaceQuadsBox->IsChecked();
  }
  if (FrustumBox)
  {
    app.Render.FrustumCulling = FrustumBox->IsChecked();
  }
  if (BatchCacheBox)
  {
    app.Render.BatchCache = BatchCacheBox->IsChecked();
  }
  // Reseed fog/sky from quality preset, then re-apply meshing / lighting toggles.
  {
    const bool greedy = app.Render.GreedyMeshing;
    const bool face = app.Render.FaceQuads;
    const bool frustum = app.Render.FrustumCulling;
    const bool batch = app.Render.BatchCache;
    const bool android_gpu = SelectedAndroidGpuEnabled;
    const bool async_mesh =
        AsyncMeshingBox ? AsyncMeshingBox->IsChecked() : app.Render.AsyncMeshing;
    app.Render = RenderSettings::FromPreset(SelectedGraphicsQuality);
    app.Render.GreedyMeshing = greedy;
    app.Render.FaceQuads = face;
    app.Render.FrustumCulling = frustum;
    app.Render.BatchCache = batch;
    app.Render.AndroidGpuEnabled = android_gpu;
    app.Render.AsyncMeshing = async_mesh;
    app.Render.Lighting = SelectedLightingMode;
    app.Render.LightingModeExplicit = true;
  }
  if (AndroidGpuBox)
  {
    app.Render.AndroidGpuEnabled = AndroidGpuBox->IsChecked();
  }
  if (LegacyHudBox)
  {
    app.Ui.LegacyHud = LegacyHudBox->IsChecked();
  }
  if (ShowPerformanceBox)
  {
    app.Ui.ShowPerformance = ShowPerformanceBox->IsChecked();
  }
  if (ConsoleKeyInput)
  {
    app.Ui.ConsoleKey = ConsoleKeyInput->GetText();
  }
  if (PaletteKeyInput)
  {
    app.Ui.PaletteKey = PaletteKeyInput->GetText();
  }
  app.Ui.HotbarCount = std::clamp(HotbarCount, 1, 2);
  app.Ui.UiScaleUser = std::clamp(UiScaleUser, kGuiMinUserScale, kGuiMaxUserScale);
  app.Ui.ControlScheme = SelectedControlScheme;
  if (PackForm)
  {
    app.DefaultResourcePacks = PackForm->ReadSelection();
    if (app.DefaultResourcePacks.WorldgenOwner.empty() &&
        !app.DefaultResourcePacks.Primary.empty())
    {
      app.DefaultResourcePacks.WorldgenOwner =
          app.DefaultResourcePacks.Primary.front();
    }
  }
  if (app.Render.GreedyMeshing && !app.Render.FaceQuads)
  {
    app.Render.FaceQuads = true;
  }

  ProceduralSettings proc =
      Host ? Host->LoadProceduralTemplate() : ProceduralSettings{};
  if (WorldForm)
  {
    const ProceduralSettings from_form = WorldForm->ReadSettings();
    proc.Generator = from_form.Generator;
    proc.Seed = from_form.Seed;
  }
  if (SelectedLightingMode == LightingMode::Flat)
  {
    proc.AsyncRelight = false;
  }
  else if (AsyncRelightBox)
  {
    proc.AsyncRelight = AsyncRelightBox->IsChecked();
  }
  Host->SaveAppAndTemplateSettings(app, proc);
  Host->ReturnToMainMenu();
}

void USettingsScreen::ShowTab(int tab)
{
  if (DialogFrame)
  {
    DialogFrame->SetActivePage(tab);
  }
}

void USettingsScreen::Build(UGuiContext &ctx)
{
  int w = ctx.GetRenderer().GetWindowWidth();
  int h = ctx.GetRenderer().GetWindowHeight();
  if (w > 0 && h > 0)
  {
    ViewportW = w;
    ViewportH = h;
  }

  const GuiTheme &theme = ctx.GetTheme();
  const AppSettingsSnapshot appSnap =
      Host ? Host->LoadAppSettingsSnapshot() : AppSettingsSnapshot{};
  const ProceduralSettings procSnap =
      Host ? Host->LoadProceduralTemplate() : ProceduralSettings{};
  HotbarCount = std::clamp(appSnap.Ui.HotbarCount, 1, 2);
  UiScaleUser =
      std::clamp(appSnap.Ui.UiScaleUser, kGuiMinUserScale, kGuiMaxUserScale);
  SelectedControlScheme = appSnap.Ui.ControlScheme;
  SelectedGraphicsQuality = appSnap.Render.Preset;
  SelectedLightingMode =
      GraphicsQualityProfile::ResolveLightingMode(appSnap.Render);
  SelectedAndroidGpuEnabled = appSnap.Render.AndroidGpuEnabled;

  auto backdrop = std::make_unique<UGuiPanel>(&theme);
  backdrop->SetBounds({0, 0, ViewportW, ViewportH});

  const int winW =
      std::min(theme.DialogDefaultWidth, ViewportW - theme.DialogMargin);
  const int winH =
      std::min(theme.DialogDefaultHeight, ViewportH - theme.DialogMargin);
  auto window = std::make_unique<UGuiWindow>(&theme, "Settings");
  Window = window.get();
  window->SetBounds(
      {(ViewportW - winW) / 2, (ViewportH - winH) / 2, winW, winH});

  auto frame = std::make_unique<UGuiDialogFrame>(&theme);
  DialogFrame = frame.get();
  frame->SetScrollbarMode(GuiScrollbarMode::Auto);
  frame->CreateTabBar({"Application", "World defaults", "Resource packs"},
                      [this](int tab) { ShowTab(tab); });

  UGuiPanel &app = frame->AddScrollPage();
  AppPanel = &app;

  auto uiScaleLbl =
      std::make_unique<UGuiLabel>(&theme, "Interface scale:");
  UiScaleLabel = uiScaleLbl.get();
  app.AddChild(std::move(uiScaleLbl));
  auto uiScaleValue = std::make_unique<UGuiLabel>(
      &theme, std::to_string(static_cast<int>(std::lround(UiScaleUser * 100))) +
                  "%");
  uiScaleValue->SetTextAlign(GuiTextAlign::Center);
  UiScaleValueLabel = uiScaleValue.get();
  app.AddChild(std::move(uiScaleValue));
  auto uiScaleSlider = std::make_unique<UGuiSlider>(&theme);
  uiScaleSlider->SetRange(kGuiMinUserScale, kGuiMaxUserScale);
  uiScaleSlider->SetStep(0.05f);
  uiScaleSlider->SetValue(UiScaleUser);
  uiScaleSlider->SetOnValueChanged(
      [this](float value)
      {
        UiScaleUser = value;
        if (UiScaleValueLabel)
        {
          UiScaleValueLabel->SetText(
              std::to_string(static_cast<int>(std::lround(UiScaleUser * 100))) +
              "%");
        }
        if (UiScaleSlider)
        {
          UiScaleSlider->SetValue(UiScaleUser);
        }
      });
  uiScaleSlider->SetOnCommit(
      [this](float /*value*/)
      {
        if (Host)
        {
          Host->ApplyLiveUiScale(UiScaleUser);
        }
      });
  UiScaleSlider = uiScaleSlider.get();
  app.AddChild(std::move(uiScaleSlider));

  auto defaultUserLbl = std::make_unique<UGuiLabel>(&theme, "Default user:");
  DefaultUserLabel = defaultUserLbl.get();
  app.AddChild(std::move(defaultUserLbl));
  auto userIn = std::make_unique<UGuiTextInput>(&theme);
  DefaultUserInput = userIn.get();
  userIn->SetText(appSnap.DefaultUser);
  app.AddChild(std::move(userIn));
  auto defaultWorldLbl =
      std::make_unique<UGuiLabel>(&theme, "Default world folder:");
  DefaultWorldLabel = defaultWorldLbl.get();
  app.AddChild(std::move(defaultWorldLbl));
  auto worldIn = std::make_unique<UGuiTextInput>(&theme);
  DefaultWorldInput = worldIn.get();
  worldIn->SetText(appSnap.DefaultWorld);
  app.AddChild(std::move(worldIn));
  auto renderDistLbl =
      std::make_unique<UGuiLabel>(&theme, "Render distance (chunks):");
  RenderDistLabel = renderDistLbl.get();
  app.AddChild(std::move(renderDistLbl));
  auto distIn = std::make_unique<UGuiTextInput>(&theme);
  RenderDistInput = distIn.get();
  distIn->SetText(std::to_string(appSnap.RenderDistanceChunks));
  app.AddChild(std::move(distIn));
  auto stream = std::make_unique<UGuiCheckbox>(&theme, "Streaming enabled");
  StreamingBox = stream.get();
  stream->SetChecked(appSnap.StreamingEnabled);
  app.AddChild(std::move(stream));
  auto step = std::make_unique<UGuiCheckbox>(&theme, "Step up");
  StepUpBox = step.get();
  step->SetChecked(appSnap.StepUpEnabled);
  app.AddChild(std::move(step));
  auto foliage = std::make_unique<UGuiCheckbox>(&theme, "Foliage climb");
  FoliageClimbBox = foliage.get();
  foliage->SetChecked(appSnap.FoliageClimbEnabled);
  app.AddChild(std::move(foliage));
  auto greedy = std::make_unique<UGuiCheckbox>(&theme, "Greedy meshing");
  GreedyBox = greedy.get();
  greedy->SetChecked(appSnap.Render.GreedyMeshing);
  app.AddChild(std::move(greedy));
  auto face = std::make_unique<UGuiCheckbox>(&theme, "Face quads");
  FaceQuadsBox = face.get();
  face->SetChecked(appSnap.Render.FaceQuads);
  app.AddChild(std::move(face));
  auto frust = std::make_unique<UGuiCheckbox>(&theme, "Frustum culling");
  FrustumBox = frust.get();
  frust->SetChecked(appSnap.Render.FrustumCulling);
  app.AddChild(std::move(frust));
  auto batch = std::make_unique<UGuiCheckbox>(&theme, "Batch cache");
  BatchCacheBox = batch.get();
  batch->SetChecked(appSnap.Render.BatchCache);
  app.AddChild(std::move(batch));
  auto hud = std::make_unique<UGuiCheckbox>(&theme, "Legacy HUD");
  LegacyHudBox = hud.get();
  hud->SetChecked(appSnap.Ui.LegacyHud);
  app.AddChild(std::move(hud));
  auto perf =
      std::make_unique<UGuiCheckbox>(&theme, "Show debug overlay");
  ShowPerformanceBox = perf.get();
  perf->SetChecked(appSnap.Ui.ShowPerformance);
  app.AddChild(std::move(perf));
  auto consoleLbl = std::make_unique<UGuiLabel>(&theme, "Console key:");
  ConsoleKeyLabel = consoleLbl.get();
  app.AddChild(std::move(consoleLbl));
  auto ckIn = std::make_unique<UGuiTextInput>(&theme);
  ConsoleKeyInput = ckIn.get();
  ckIn->SetText(appSnap.Ui.ConsoleKey);
  app.AddChild(std::move(ckIn));
  auto paletteLbl = std::make_unique<UGuiLabel>(&theme, "Palette key:");
  PaletteKeyLabel = paletteLbl.get();
  app.AddChild(std::move(paletteLbl));
  auto pkIn = std::make_unique<UGuiTextInput>(&theme);
  PaletteKeyInput = pkIn.get();
  pkIn->SetText(appSnap.Ui.PaletteKey);
  app.AddChild(std::move(pkIn));
  auto hotbarLbl = std::make_unique<UGuiLabel>(&theme, "Hotbar count:");
  HotbarCountLabel = hotbarLbl.get();
  app.AddChild(std::move(hotbarLbl));
  auto hotbarValue =
      std::make_unique<UGuiLabel>(&theme, std::to_string(HotbarCount));
  hotbarValue->SetTextAlign(GuiTextAlign::Center);
  HotbarCountValueLabel = hotbarValue.get();
  app.AddChild(std::move(hotbarValue));
  auto hotbarMinus = std::make_unique<UGuiButton>(&theme, "-");
  hotbarMinus->SetOnClick(
      [this]()
      {
        HotbarCount = std::max(1, HotbarCount - 1);
        if (HotbarCountValueLabel)
        {
          HotbarCountValueLabel->SetText(std::to_string(HotbarCount));
        }
      });
  HotbarMinusButton = hotbarMinus.get();
  app.AddChild(std::move(hotbarMinus));
  auto hotbarPlus = std::make_unique<UGuiButton>(&theme, "+");
  hotbarPlus->SetOnClick(
      [this]()
      {
        HotbarCount = std::min(2, HotbarCount + 1);
        if (HotbarCountValueLabel)
        {
          HotbarCountValueLabel->SetText(std::to_string(HotbarCount));
        }
      });
  HotbarPlusButton = hotbarPlus.get();
  app.AddChild(std::move(hotbarPlus));

  auto controlSchemeLbl =
      std::make_unique<UGuiLabel>(&theme, "Control scheme:");
  ControlSchemeLabel = controlSchemeLbl.get();
  app.AddChild(std::move(controlSchemeLbl));
  auto profileBtn = std::make_unique<UGuiButton>(
      &theme,
      SelectedControlScheme == ControlScheme::Cubatarium ? "Cubatarium" : "Classic");
  profileBtn->SetOnClick(
      [this]()
      {
        SelectedControlScheme = SelectedControlScheme == ControlScheme::Classic
                            ? ControlScheme::Cubatarium
                            : ControlScheme::Classic;
        if (ControlSchemeButton)
        {
          ControlSchemeButton->SetLabel(
              SelectedControlScheme == ControlScheme::Cubatarium ? "Cubatarium"
                                                         : "Classic");
        }
      });
  ControlSchemeButton = profileBtn.get();
  app.AddChild(std::move(profileBtn));

  auto graphicsQualityLbl =
      std::make_unique<UGuiLabel>(&theme, "Graphics quality:");
  GraphicsQualityLabel = graphicsQualityLbl.get();
  app.AddChild(std::move(graphicsQualityLbl));
  auto graphicsQualityBtn = std::make_unique<UGuiButton>(
      &theme, GraphicsQualityProfile::DisplayName(SelectedGraphicsQuality));
  graphicsQualityBtn->SetOnClick(
      [this]()
      {
        SelectedGraphicsQuality =
            GraphicsQualityProfile::NextPreset(SelectedGraphicsQuality);
        if (GraphicsQualityButton)
        {
          GraphicsQualityButton->SetLabel(
              GraphicsQualityProfile::DisplayName(SelectedGraphicsQuality));
        }
      });
  GraphicsQualityButton = graphicsQualityBtn.get();
  app.AddChild(std::move(graphicsQualityBtn));

  auto lightingLbl = std::make_unique<UGuiLabel>(&theme, "Lighting:");
  LightingModeLabel = lightingLbl.get();
  app.AddChild(std::move(lightingLbl));
  auto lightingBtn = std::make_unique<UGuiButton>(
      &theme, GraphicsQualityProfile::LightingDisplayName(SelectedLightingMode));
  lightingBtn->SetOnClick(
      [this]()
      {
        SelectedLightingMode =
            GraphicsQualityProfile::NextLightingMode(SelectedLightingMode);
        if (LightingModeButton)
        {
          LightingModeButton->SetLabel(
              GraphicsQualityProfile::LightingDisplayName(SelectedLightingMode));
        }
        if (AsyncRelightBox)
        {
          AsyncRelightBox->SetEnabled(SelectedLightingMode ==
                                      LightingMode::Full);
          if (SelectedLightingMode == LightingMode::Flat)
          {
            AsyncRelightBox->SetChecked(false);
          }
        }
      });
  LightingModeButton = lightingBtn.get();
  app.AddChild(std::move(lightingBtn));

  auto asyncMesh = std::make_unique<UGuiCheckbox>(&theme, "Async meshing");
  asyncMesh->SetChecked(appSnap.Render.AsyncMeshing);
  AsyncMeshingBox = asyncMesh.get();
  app.AddChild(std::move(asyncMesh));

  auto asyncRelight =
      std::make_unique<UGuiCheckbox>(&theme, "Async relight (Full only)");
  asyncRelight->SetChecked(procSnap.AsyncRelight &&
                           SelectedLightingMode == LightingMode::Full);
  asyncRelight->SetEnabled(SelectedLightingMode == LightingMode::Full);
  AsyncRelightBox = asyncRelight.get();
  app.AddChild(std::move(asyncRelight));

#if defined(__ANDROID__) || defined(CUBATARIUM_GLES)
  auto androidGpuBox = std::make_unique<UGuiCheckbox>(
      &theme, "GPU render (restart world)");
  androidGpuBox->SetChecked(SelectedAndroidGpuEnabled);
  androidGpuBox->SetOnChanged(
      [this](bool checked) { SelectedAndroidGpuEnabled = checked; });
  AndroidGpuBox = androidGpuBox.get();
  app.AddChild(std::move(androidGpuBox));
#endif

  UGuiPanel &world = frame->AddScrollPage();
  WorldPanel = &world;
  WorldForm = std::make_unique<UWorldGenSettingsForm>(&theme);
  WorldForm->SetSettings(procSnap);
  WorldForm->BuildInto(world);

  UGuiPanel &packs = frame->AddScrollPage();
  PacksPanel = &packs;
  PackForm = std::make_unique<UResourcePackPickerForm>(&theme);
  PackForm->SetPacks(Host ? Host->ListInstalledResourcePacks()
                          : std::vector<InstalledPackInfo>{});
  PackForm->SetSelection(appSnap.DefaultResourcePacks.Primary.empty()
                             ? (Host ? Host->GetDefaultResourcePackSelection()
                                     : ResourcePackSelection{})
                             : appSnap.DefaultResourcePacks);
  PackForm->BuildInto(packs);

  frame->SetScrollPageLayout(
      0, [this](const GuiRect &area) { return MeasureAppPageHeight(area); },
      [this](const GuiRect &area) { LayoutAppPage(area); });
  frame->SetScrollPageLayout(
      1, [this](const GuiRect &area) { return MeasureWorldPageHeight(area); },
      [this](const GuiRect &area) { LayoutWorldPage(area); });
  frame->SetScrollPageLayout(
      2, [this](const GuiRect &area) { return MeasurePacksPageHeight(area); },
      [this](const GuiRect &area) { LayoutPacksPage(area); });

  auto saveBtn = std::make_unique<UGuiButton>(&theme, "Save");
  saveBtn->SetOnClick([this]() { OnSave(); });
  frame->AddFooterButton(std::move(saveBtn));
  auto cancelBtn = std::make_unique<UGuiButton>(&theme, "Cancel");
  cancelBtn->SetOnClick(
      [this]()
      {
        if (Host)
        {
          Host->ReturnToMainMenu();
        }
      });
  frame->AddFooterButton(std::move(cancelBtn));

  window->AddChild(std::move(frame));
  backdrop->AddChild(std::move(window));
  Root = std::move(backdrop);

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
  if (!Window || !DialogFrame)
  {
    return;
  }
  const GuiTheme *theme = GetMetrics().Theme;
  if (!theme)
  {
    return;
  }
  const int winW =
      std::min(theme->DialogDefaultWidth, ViewportW - theme->DialogMargin);
  const int winH =
      std::min(theme->DialogDefaultHeight, ViewportH - theme->DialogMargin);
  Window->SetBounds(
      {(ViewportW - winW) / 2, (ViewportH - winH) / 2, winW, winH});
  DialogFrame->SetBounds(Window->GetClientArea());
  DialogFrame->LayoutFrame();
}

std::vector<GuiGridItem>
USettingsScreen::BuildAppGridItems(const GuiGridSpec &spec) const
{
  const int cols = std::max(1, spec.columns);
  const int labelH = Scaled(28);
  const int fieldH = Scaled(32);
  const int checkH = Scaled(30);
  const int sliderH = Scaled(28);
  std::vector<GuiGridItem> items;
  int row = 0;

  auto addLabeledField = [&](UGuiWidget *label, UGuiWidget *field)
  {
    if (cols > 1)
    {
      items.push_back({label, row, 0, 1, 1, labelH});
      items.push_back({field, row, 1, 1, 1, fieldH});
      ++row;
      return;
    }
    items.push_back({label, row, 0, 1, 1, labelH});
    ++row;
    items.push_back({field, row, 0, 1, 1, fieldH});
    ++row;
  };

  auto addCheckPair = [&](UGuiWidget *left, UGuiWidget *right)
  {
    if (cols > 1)
    {
      items.push_back({left, row, 0, 1, 1, checkH});
      items.push_back({right, row, 1, 1, 1, checkH});
      ++row;
      return;
    }
    items.push_back({left, row++, 0, 1, 1, checkH});
    items.push_back({right, row++, 0, 1, 1, checkH});
  };

  items.push_back({UiScaleLabel, row, 0, 1, 1, labelH});
  if (cols > 1)
  {
    items.push_back({UiScaleValueLabel, row, 1, 1, 1, fieldH});
    ++row;
    items.push_back({UiScaleSlider, row, 0, 1, cols, sliderH});
    ++row;
  }
  else
  {
    ++row;
    items.push_back({UiScaleValueLabel, row, 0, 1, 1, fieldH});
    ++row;
    items.push_back({UiScaleSlider, row, 0, 1, 1, sliderH});
    ++row;
  }

  addLabeledField(DefaultUserLabel, DefaultUserInput);
  addLabeledField(DefaultWorldLabel, DefaultWorldInput);
  addLabeledField(RenderDistLabel, RenderDistInput);
  addCheckPair(StreamingBox, StepUpBox);
  items.push_back({FoliageClimbBox, row, 0, 1, cols, checkH});
  ++row;
  addCheckPair(GreedyBox, FaceQuadsBox);
  addCheckPair(FrustumBox, BatchCacheBox);
  items.push_back({LegacyHudBox, row, 0, 1, cols, checkH});
  ++row;
  items.push_back({ShowPerformanceBox, row, 0, 1, cols, checkH});
  ++row;
  addLabeledField(ConsoleKeyLabel, ConsoleKeyInput);
  addLabeledField(PaletteKeyLabel, PaletteKeyInput);

  items.push_back({HotbarCountLabel, row, 0, 1, 1, labelH});
  if (cols > 1)
  {
    items.push_back({HotbarCountValueLabel, row, 1, 1, 1, fieldH});
    ++row;
  }
  else
  {
    ++row;
    items.push_back({HotbarCountValueLabel, row, 0, 1, 1, fieldH});
    ++row;
  }
  addLabeledField(ControlSchemeLabel, ControlSchemeButton);
  addLabeledField(GraphicsQualityLabel, GraphicsQualityButton);
  addLabeledField(LightingModeLabel, LightingModeButton);
  addCheckPair(AsyncMeshingBox, AsyncRelightBox);
  if (AndroidGpuBox)
  {
    items.push_back({AndroidGpuBox, row, 0, 1, cols, checkH});
    ++row;
  }
  return items;
}

void USettingsScreen::LayoutHotbarCountControls(const GuiGridSpec &spec) const
{
  if (!HotbarCountValueLabel || !HotbarMinusButton || !HotbarPlusButton)
  {
    return;
  }

  const int btnSize = Scaled(32);
  const int valueW = Scaled(28);
  const int gap = Scaled(6);

  if (spec.columns <= 1)
  {
    const GuiRect anchor = HotbarCountValueLabel->GetBounds();
    const int y = anchor.Y + (anchor.H - btnSize) / 2;
    const int x = anchor.X;
    HotbarCountValueLabel->SetBounds({x, y, valueW, btnSize});
    HotbarPlusButton->SetBounds({x + valueW + gap, y, btnSize, btnSize});
    HotbarMinusButton->SetBounds(
        {x + valueW + gap + btnSize + gap, y, btnSize, btnSize});
    return;
  }

  const GuiRect cell = HotbarCountValueLabel->GetBounds();
  const int y = cell.Y + (cell.H - btnSize) / 2;
  const int startX = cell.X;
  HotbarCountValueLabel->SetBounds({startX, y, valueW, btnSize});
  HotbarPlusButton->SetBounds({startX + valueW + gap, y, btnSize, btnSize});
  HotbarMinusButton->SetBounds(
      {startX + valueW + gap + btnSize + gap, y, btnSize, btnSize});
}

int USettingsScreen::MeasureAppPageHeight(const GuiRect &area) const
{
  const GuiGridSpec spec = BuildTwoColumnSpec(GetMetrics(), area.W);
  return UGuiLayout::GridMeasure(area, spec, BuildAppGridItems(spec));
}

void USettingsScreen::LayoutAppPage(const GuiRect &area) const
{
  const GuiGridSpec spec = BuildTwoColumnSpec(GetMetrics(), area.W);
  UGuiLayout::GridPlace(area, spec, BuildAppGridItems(spec));
  LayoutHotbarCountControls(spec);
}

int USettingsScreen::MeasureWorldPageHeight(const GuiRect &area) const
{
  if (!WorldForm)
  {
    return 0;
  }
  return WorldForm->MeasureGridHeight(area, BuildTwoColumnSpec(GetMetrics(), area.W));
}

void USettingsScreen::LayoutWorldPage(const GuiRect &area) const
{
  if (!WorldForm)
  {
    return;
  }
  WorldForm->LayoutGrid(area, BuildTwoColumnSpec(GetMetrics(), area.W));
}

int USettingsScreen::MeasurePacksPageHeight(const GuiRect &area) const
{
  if (!PackForm)
  {
    return 0;
  }
  return PackForm->MeasureHeight(area);
}

void USettingsScreen::LayoutPacksPage(const GuiRect &area) const
{
  if (!PackForm)
  {
    return;
  }
  PackForm->Layout({area.X, area.Y, area.W, PackForm->MeasureHeight(area)});
}

} // namespace cutum
