#include "SettingsScreen.h"
#include "AppSettingsSnapshot.h"
#include "Gui/GuiContext.h"
#include "Gui/GuiRenderer.h"
#include "Gui/Interfaces/IGuiMenuHost.h"
#include "Gui/Widgets/GuiButton.h"
#include "Gui/Widgets/GuiCheckbox.h"
#include "Gui/Widgets/GuiDialogFrame.h"
#include "Gui/Widgets/GuiLabel.h"
#include "Gui/Widgets/GuiPanel.h"
#include "Gui/Widgets/GuiTextInput.h"
#include "Gui/Widgets/GuiWindow.h"
#include "Gui/Widgets/WorldGenSettingsForm.h"
#include "Gui/Layout/GuiLayout.h"
#include <algorithm>

namespace cutum {

namespace {

int ParseIntOr(const std::string& text, int fallback)
{
    try {
        return std::stoi(text);
    } catch (...) {
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

SettingsScreen::SettingsScreen(IGuiMenuHost* host)
    : host_(host)
{
}

void SettingsScreen::OnSave()
{
    if (!host_) {
        return;
    }
    AppSettingsSnapshot app = host_->LoadAppSettingsSnapshot();
    if (defaultUserInput_) {
        app.defaultUser = defaultUserInput_->GetText();
    }
    if (defaultWorldInput_) {
        app.defaultWorld = defaultWorldInput_->GetText();
    }
    if (renderDistInput_) {
        app.renderDistanceChunks = ParseIntOr(renderDistInput_->GetText(), app.renderDistanceChunks);
    }
    if (streamingBox_) {
        app.streamingEnabled = streamingBox_->IsChecked();
    }
    if (stepUpBox_) {
        app.stepUpEnabled = stepUpBox_->IsChecked();
    }
    if (greedyBox_) {
        app.render.greedyMeshing = greedyBox_->IsChecked();
    }
    if (faceQuadsBox_) {
        app.render.faceQuads = faceQuadsBox_->IsChecked();
    }
    if (frustumBox_) {
        app.render.frustumCulling = frustumBox_->IsChecked();
    }
    if (batchCacheBox_) {
        app.render.batchCache = batchCacheBox_->IsChecked();
    }
    if (legacyHudBox_) {
        app.ui.legacyHud = legacyHudBox_->IsChecked();
    }
    if (consoleKeyInput_) {
        app.ui.consoleKey = consoleKeyInput_->GetText();
    }
    if (paletteKeyInput_) {
        app.ui.paletteKey = paletteKeyInput_->GetText();
    }
    app.ui.hotbarCount = std::clamp(hotbarCount_, 1, 2);
    if (app.render.greedyMeshing && !app.render.faceQuads) {
        app.render.faceQuads = true;
    }

    ProceduralSettings proc = worldForm_ ? worldForm_->ReadSettings() : host_->LoadProceduralTemplate();
    host_->SaveAppAndTemplateSettings(app, proc);
    host_->ReturnToMainMenu();
}

void SettingsScreen::ShowTab(int tab)
{
    if (dialogFrame_) {
        dialogFrame_->SetActivePage(tab);
    }
}

void SettingsScreen::Build(GuiContext& ctx)
{
    int w = ctx.GetRenderer().GetWindowWidth();
    int h = ctx.GetRenderer().GetWindowHeight();
    if (w > 0 && h > 0) {
        viewportW_ = w;
        viewportH_ = h;
    }

    const GuiTheme& theme = ctx.GetTheme();
    const AppSettingsSnapshot appSnap = host_ ? host_->LoadAppSettingsSnapshot() : AppSettingsSnapshot{};
    const ProceduralSettings procSnap = host_ ? host_->LoadProceduralTemplate() : ProceduralSettings{};
    hotbarCount_ = std::clamp(appSnap.ui.hotbarCount, 1, 2);

    auto backdrop = std::make_unique<GuiPanel>(&theme);
    backdrop->SetBounds({0, 0, viewportW_, viewportH_});

    const int winW = std::min(860, viewportW_ - 32);
    const int winH = std::min(540, viewportH_ - 32);
    auto window = std::make_unique<GuiWindow>(&theme, "Settings");
    window_ = window.get();
    window->SetBounds({(viewportW_ - winW) / 2, (viewportH_ - winH) / 2, winW, winH});

    auto frame = std::make_unique<GuiDialogFrame>(&theme);
    dialogFrame_ = frame.get();
    frame->SetScrollbarMode(GuiScrollbarMode::Hidden);
    frame->CreateTabBar({"Application", "World defaults"}, [this](int tab) { ShowTab(tab); });

    GuiPanel& app = frame->AddScrollPage();
    appPanel_ = &app;
    auto defaultUserLbl = std::make_unique<GuiLabel>(&theme, "Default user:");
    defaultUserLabel_ = defaultUserLbl.get();
    app.AddChild(std::move(defaultUserLbl));
    auto userIn = std::make_unique<GuiTextInput>(&theme);
    defaultUserInput_ = userIn.get();
    userIn->SetText(appSnap.defaultUser);
    app.AddChild(std::move(userIn));
    auto defaultWorldLbl = std::make_unique<GuiLabel>(&theme, "Default world folder:");
    defaultWorldLabel_ = defaultWorldLbl.get();
    app.AddChild(std::move(defaultWorldLbl));
    auto worldIn = std::make_unique<GuiTextInput>(&theme);
    defaultWorldInput_ = worldIn.get();
    worldIn->SetText(appSnap.defaultWorld);
    app.AddChild(std::move(worldIn));
    auto renderDistLbl = std::make_unique<GuiLabel>(&theme, "Render distance (chunks):");
    renderDistLabel_ = renderDistLbl.get();
    app.AddChild(std::move(renderDistLbl));
    auto distIn = std::make_unique<GuiTextInput>(&theme);
    renderDistInput_ = distIn.get();
    distIn->SetText(std::to_string(appSnap.renderDistanceChunks));
    app.AddChild(std::move(distIn));
    auto stream = std::make_unique<GuiCheckbox>(&theme, "Streaming enabled");
    streamingBox_ = stream.get();
    stream->SetChecked(appSnap.streamingEnabled);
    app.AddChild(std::move(stream));
    auto step = std::make_unique<GuiCheckbox>(&theme, "Step up");
    stepUpBox_ = step.get();
    step->SetChecked(appSnap.stepUpEnabled);
    app.AddChild(std::move(step));
    auto greedy = std::make_unique<GuiCheckbox>(&theme, "Greedy meshing");
    greedyBox_ = greedy.get();
    greedy->SetChecked(appSnap.render.greedyMeshing);
    app.AddChild(std::move(greedy));
    auto face = std::make_unique<GuiCheckbox>(&theme, "Face quads");
    faceQuadsBox_ = face.get();
    face->SetChecked(appSnap.render.faceQuads);
    app.AddChild(std::move(face));
    auto frust = std::make_unique<GuiCheckbox>(&theme, "Frustum culling");
    frustumBox_ = frust.get();
    frust->SetChecked(appSnap.render.frustumCulling);
    app.AddChild(std::move(frust));
    auto batch = std::make_unique<GuiCheckbox>(&theme, "Batch cache");
    batchCacheBox_ = batch.get();
    batch->SetChecked(appSnap.render.batchCache);
    app.AddChild(std::move(batch));
    auto hud = std::make_unique<GuiCheckbox>(&theme, "Legacy HUD");
    legacyHudBox_ = hud.get();
    hud->SetChecked(appSnap.ui.legacyHud);
    app.AddChild(std::move(hud));
    auto consoleLbl = std::make_unique<GuiLabel>(&theme, "Console key:");
    consoleKeyLabel_ = consoleLbl.get();
    app.AddChild(std::move(consoleLbl));
    auto ckIn = std::make_unique<GuiTextInput>(&theme);
    consoleKeyInput_ = ckIn.get();
    ckIn->SetText(appSnap.ui.consoleKey);
    app.AddChild(std::move(ckIn));
    auto paletteLbl = std::make_unique<GuiLabel>(&theme, "Palette key:");
    paletteKeyLabel_ = paletteLbl.get();
    app.AddChild(std::move(paletteLbl));
    auto pkIn = std::make_unique<GuiTextInput>(&theme);
    paletteKeyInput_ = pkIn.get();
    pkIn->SetText(appSnap.ui.paletteKey);
    app.AddChild(std::move(pkIn));
    auto hotbarLbl = std::make_unique<GuiLabel>(&theme, "Hotbar count:");
    hotbarCountLabel_ = hotbarLbl.get();
    app.AddChild(std::move(hotbarLbl));
    auto hotbarValue = std::make_unique<GuiLabel>(&theme, std::to_string(hotbarCount_));
    hotbarValue->SetTextAlign(GuiTextAlign::Center);
    hotbarCountValueLabel_ = hotbarValue.get();
    app.AddChild(std::move(hotbarValue));
    auto hotbarMinus = std::make_unique<GuiButton>(&theme, "-");
    hotbarMinus->SetOnClick([this]() {
        hotbarCount_ = std::max(1, hotbarCount_ - 1);
        if (hotbarCountValueLabel_) {
            hotbarCountValueLabel_->SetText(std::to_string(hotbarCount_));
        }
    });
    hotbarMinusButton_ = hotbarMinus.get();
    app.AddChild(std::move(hotbarMinus));
    auto hotbarPlus = std::make_unique<GuiButton>(&theme, "+");
    hotbarPlus->SetOnClick([this]() {
        hotbarCount_ = std::min(2, hotbarCount_ + 1);
        if (hotbarCountValueLabel_) {
            hotbarCountValueLabel_->SetText(std::to_string(hotbarCount_));
        }
    });
    hotbarPlusButton_ = hotbarPlus.get();
    app.AddChild(std::move(hotbarPlus));

    GuiPanel& world = frame->AddScrollPage();
    worldPanel_ = &world;
    worldForm_ = std::make_unique<WorldGenSettingsForm>(&theme);
    worldForm_->SetSettings(procSnap);
    worldForm_->BuildInto(world);
    frame->SetScrollPageLayout(
        0,
        [this](const GuiRect& area) { return MeasureAppPageHeight(area); },
        [this](const GuiRect& area) { LayoutAppPage(area); });
    frame->SetScrollPageLayout(
        1,
        [this](const GuiRect& area) { return MeasureWorldPageHeight(area); },
        [this](const GuiRect& area) { LayoutWorldPage(area); });

    auto saveBtn = std::make_unique<GuiButton>(&theme, "Save");
    saveBtn->SetOnClick([this]() { OnSave(); });
    frame->AddFooterButton(std::move(saveBtn));
    auto cancelBtn = std::make_unique<GuiButton>(&theme, "Cancel");
    cancelBtn->SetOnClick([this]() {
        if (host_) {
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

void SettingsScreen::OnViewportChanged(int width, int height)
{
    GuiScreenBase::OnViewportChanged(width, height);
    Relayout();
}

void SettingsScreen::Relayout()
{
    if (!window_ || !dialogFrame_) {
        return;
    }
    const int winW = std::min(860, viewportW_ - 32);
    const int winH = std::min(540, viewportH_ - 32);
    window_->SetBounds({(viewportW_ - winW) / 2, (viewportH_ - winH) / 2, winW, winH});
    dialogFrame_->SetBounds(window_->GetClientArea());
    dialogFrame_->LayoutFrame();
}

int SettingsScreen::MeasureAppPageHeight(const GuiRect& area) const
{
    const GuiGridSpec spec = BuildTwoColumnSpec(area.w);
    std::vector<GuiGridItem> items{
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
        {hotbarCountValueLabel_, 9, 1, 1, 1, 32},
        {hotbarMinusButton_, 10, 0, 1, 1, 32},
        {hotbarPlusButton_, 10, 1, 1, 1, 32},
    };
    return GuiLayout::GridMeasure(area, spec, items);
}

void SettingsScreen::LayoutAppPage(const GuiRect& area) const
{
    const GuiGridSpec spec = BuildTwoColumnSpec(area.w);
    std::vector<GuiGridItem> items{
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
        {hotbarCountValueLabel_, 9, 1, 1, 1, 32},
        {hotbarMinusButton_, 10, 0, 1, 1, 32},
        {hotbarPlusButton_, 10, 1, 1, 1, 32},
    };
    GuiLayout::GridPlace(area, spec, items);
}

int SettingsScreen::MeasureWorldPageHeight(const GuiRect& area) const
{
    if (!worldForm_) {
        return 0;
    }
    return worldForm_->MeasureGridHeight(area, BuildTwoColumnSpec(area.w));
}

void SettingsScreen::LayoutWorldPage(const GuiRect& area) const
{
    if (!worldForm_) {
        return;
    }
    worldForm_->LayoutGrid(area, BuildTwoColumnSpec(area.w));
}

} // namespace cutum
