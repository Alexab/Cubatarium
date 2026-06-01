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

    auto backdrop = std::make_unique<GuiPanel>(&theme);
    backdrop->SetBounds({0, 0, viewportW_, viewportH_});

    const int winW = std::min(520, viewportW_ - 40);
    const int winH = std::min(560, viewportH_ - 40);
    auto window = std::make_unique<GuiWindow>(&theme, "Settings");
    window_ = window.get();
    window->SetBounds({(viewportW_ - winW) / 2, (viewportH_ - winH) / 2, winW, winH});

    auto frame = std::make_unique<GuiDialogFrame>(&theme);
    dialogFrame_ = frame.get();
    frame->CreateTabBar({"Application", "World defaults"}, [this](int tab) { ShowTab(tab); });

    GuiPanel& app = frame->AddScrollPage();
    appPanel_ = &app;
    app.AddChild(std::make_unique<GuiLabel>(&theme, "Default user:"));
    auto userIn = std::make_unique<GuiTextInput>(&theme);
    defaultUserInput_ = userIn.get();
    userIn->SetText(appSnap.defaultUser);
    app.AddChild(std::move(userIn));
    app.AddChild(std::make_unique<GuiLabel>(&theme, "Default world folder:"));
    auto worldIn = std::make_unique<GuiTextInput>(&theme);
    defaultWorldInput_ = worldIn.get();
    worldIn->SetText(appSnap.defaultWorld);
    app.AddChild(std::move(worldIn));
    app.AddChild(std::make_unique<GuiLabel>(&theme, "Render distance (chunks):"));
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
    app.AddChild(std::make_unique<GuiLabel>(&theme, "Console key:"));
    auto ckIn = std::make_unique<GuiTextInput>(&theme);
    consoleKeyInput_ = ckIn.get();
    ckIn->SetText(appSnap.ui.consoleKey);
    app.AddChild(std::move(ckIn));
    app.AddChild(std::make_unique<GuiLabel>(&theme, "Palette key:"));
    auto pkIn = std::make_unique<GuiTextInput>(&theme);
    paletteKeyInput_ = pkIn.get();
    pkIn->SetText(appSnap.ui.paletteKey);
    app.AddChild(std::move(pkIn));

    GuiPanel& world = frame->AddScrollPage();
    worldPanel_ = &world;
    worldForm_ = std::make_unique<WorldGenSettingsForm>(&theme);
    worldForm_->SetSettings(procSnap);
    worldForm_->BuildInto(world);

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
    const int winW = std::min(520, viewportW_ - 40);
    const int winH = std::min(560, viewportH_ - 40);
    window_->SetBounds({(viewportW_ - winW) / 2, (viewportH_ - winH) / 2, winW, winH});
    dialogFrame_->SetBounds(window_->GetClientArea());
    dialogFrame_->LayoutFrame();
}

} // namespace cutum
