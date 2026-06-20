#include "Gui/Screens/SettingsScreen.h"
#include "App/Settings/AppSettingsSnapshot.h"
#include "App/Settings/UiSettings.h"
#include "ResourcePacks/ResourcePackResolver.h"
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
#include "Gui/Widgets/ResourcePackPickerForm.h"
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
  spec.Padding = 4;
  spec.columnWeights = {1, 1};
  return spec;
}

} // namespace

USettingsScreen::USettingsScreen(IGuiMenuHost *host) : Host(host) {}

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
      WorldForm ? WorldForm->ReadSettings() : Host->LoadProceduralTemplate();
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
  SelectedControlScheme = appSnap.Ui.ControlScheme;

  auto backdrop = std::make_unique<UGuiPanel>(&theme);
  backdrop->SetBounds({0, 0, ViewportW, ViewportH});

  const int winW = std::min(860, ViewportW - 32);
  const int winH = std::min(540, ViewportH - 32);
  auto window = std::make_unique<UGuiWindow>(&theme, "Settings");
  Window = window.get();
  window->SetBounds(
      {(ViewportW - winW) / 2, (ViewportH - winH) / 2, winW, winH});

  auto frame = std::make_unique<UGuiDialogFrame>(&theme);
  DialogFrame = frame.get();
  frame->SetScrollbarMode(GuiScrollbarMode::Hidden);
  frame->CreateTabBar({"Application", "World defaults", "Resource packs"},
                      [this](int tab) { ShowTab(tab); });

  UGuiPanel &app = frame->AddScrollPage();
  AppPanel = &app;
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
      std::make_unique<UGuiCheckbox>(&theme, "Show performance overlay");
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
  const int winW = std::min(860, ViewportW - 32);
  const int winH = std::min(540, ViewportH - 32);
  Window->SetBounds(
      {(ViewportW - winW) / 2, (ViewportH - winH) / 2, winW, winH});
  DialogFrame->SetBounds(Window->GetClientArea());
  DialogFrame->LayoutFrame();
}

std::vector<GuiGridItem>
USettingsScreen::BuildAppGridItems(const GuiGridSpec &spec) const
{
  const int hotbarValueRow = spec.columns > 1 ? 10 : 11;
  const int hotbarValueCol = spec.columns > 1 ? 1 : 0;
  return {
      {DefaultUserLabel, 0, 0, 1, 1, 28},
      {DefaultUserInput, 0, 1, 1, 1, 32},
      {DefaultWorldLabel, 1, 0, 1, 1, 28},
      {DefaultWorldInput, 1, 1, 1, 1, 32},
      {RenderDistLabel, 2, 0, 1, 1, 28},
      {RenderDistInput, 2, 1, 1, 1, 32},
      {StreamingBox, 3, 0, 1, 1, 30},
      {StepUpBox, 3, 1, 1, 1, 30},
      {GreedyBox, 4, 0, 1, 1, 30},
      {FaceQuadsBox, 4, 1, 1, 1, 30},
      {FrustumBox, 5, 0, 1, 1, 30},
      {BatchCacheBox, 5, 1, 1, 1, 30},
      {LegacyHudBox, 6, 0, 1, 2, 30},
      {ShowPerformanceBox, 7, 0, 1, 2, 30},
      {ConsoleKeyLabel, 8, 0, 1, 1, 28},
      {ConsoleKeyInput, 8, 1, 1, 1, 32},
      {PaletteKeyLabel, 9, 0, 1, 1, 28},
      {PaletteKeyInput, 9, 1, 1, 1, 32},
      {HotbarCountLabel, 10, 0, 1, 1, 28},
      {HotbarCountValueLabel, hotbarValueRow, hotbarValueCol, 1, 1, 32},
      {ControlSchemeLabel, 11, 0, 1, 1, 28},
      {ControlSchemeButton, 11, 1, 1, 1, 32},
  };
}

void USettingsScreen::LayoutHotbarCountControls(const GuiGridSpec &spec) const
{
  if (!HotbarCountValueLabel || !HotbarMinusButton || !HotbarPlusButton)
  {
    return;
  }

  constexpr int btnSize = 32;
  constexpr int valueW = 28;
  constexpr int gap = 6;

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
  const GuiGridSpec spec = BuildTwoColumnSpec(area.W);
  return UGuiLayout::GridMeasure(area, spec, BuildAppGridItems(spec));
}

void USettingsScreen::LayoutAppPage(const GuiRect &area) const
{
  const GuiGridSpec spec = BuildTwoColumnSpec(area.W);
  UGuiLayout::GridPlace(area, spec, BuildAppGridItems(spec));
  LayoutHotbarCountControls(spec);
}

int USettingsScreen::MeasureWorldPageHeight(const GuiRect &area) const
{
  if (!WorldForm)
  {
    return 0;
  }
  return WorldForm->MeasureGridHeight(area, BuildTwoColumnSpec(area.W));
}

void USettingsScreen::LayoutWorldPage(const GuiRect &area) const
{
  if (!WorldForm)
  {
    return;
  }
  WorldForm->LayoutGrid(area, BuildTwoColumnSpec(area.W));
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
