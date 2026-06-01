#include "GuiContext.h"
#include "GuiRenderer.h"
#include "GuiInputRouter.h"
#include "GuiScreenBase.h"
#include "Gui/Widgets/GuiWidget.h"
#include "ShaderManager.h"
#include "TextRenderer.h"

namespace cutum {

GuiContext::GuiContext() = default;

GuiContext::~GuiContext()
{
    Shutdown();
}

bool GuiContext::Initialize(std::shared_ptr<ShaderManager> shaderManager,
                            std::shared_ptr<TextRenderer> textRenderer)
{
    theme_ = DefaultGuiTheme();
    renderer_ = std::make_unique<GuiRenderer>();
    inputRouter_ = std::make_unique<GuiInputRouter>();
    return renderer_->Initialize(std::move(shaderManager), std::move(textRenderer));
}

void GuiContext::Shutdown()
{
    if (inputRouter_) {
        inputRouter_->ClearInteractionState();
        inputRouter_->SetActiveScreen(nullptr);
    }
    if (activeScreen_) {
        activeScreen_->OnDetach();
        activeScreen_.reset();
    }
    if (renderer_) {
        renderer_->Shutdown();
        renderer_.reset();
    }
    if (inputRouter_) {
        inputRouter_->ReleaseFocusWithoutNotify();
        inputRouter_->SetRoot(nullptr);
    }
}

void GuiContext::SetScreen(std::unique_ptr<GuiScreenBase> screen)
{
    if (inputRouter_) {
        inputRouter_->ClearInteractionState();
    }
    if (activeScreen_) {
        activeScreen_->OnDetach();
    }
    activeScreen_ = std::move(screen);
    if (activeScreen_) {
        activeScreen_->OnAttach(*this);
        activeScreen_->Build(*this);
        inputRouter_->SetRoot(activeScreen_->GetRoot());
        inputRouter_->SetActiveScreen(activeScreen_.get());
    } else {
        inputRouter_->SetRoot(nullptr);
        inputRouter_->SetActiveScreen(nullptr);
    }
}

void GuiContext::Update(double dt)
{
    if (activeScreen_) {
        activeScreen_->Update(dt);
        if (activeScreen_->GetRoot()) {
            activeScreen_->GetRoot()->Update(dt);
        }
    }
}

void GuiContext::NotifyViewport(int windowWidth, int windowHeight)
{
    if (activeScreen_) {
        activeScreen_->OnViewportChanged(windowWidth, windowHeight);
    }
}

void GuiContext::Render(int windowWidth, int windowHeight)
{
    if (!renderer_ || !activeScreen_ || !activeScreen_->GetRoot()) {
        return;
    }
    NotifyViewport(windowWidth, windowHeight);
    RenderOverlay(*activeScreen_->GetRoot(), windowWidth, windowHeight);
}

void GuiContext::RenderOverlay(GuiWidget& root, int windowWidth, int windowHeight,
                               bool expandRootToViewport)
{
    if (!renderer_) {
        return;
    }
    renderer_->BeginFrame(windowWidth, windowHeight);
    if (expandRootToViewport) {
        NotifyViewport(windowWidth, windowHeight);
        const GuiRect full{0, 0, windowWidth, windowHeight};
        root.SetBounds(full);
        root.UpdateLayout(full);
    } else {
        root.UpdateLayout(root.GetBounds());
    }
    root.Draw(*renderer_);
    renderer_->EndFrame();
}

bool GuiContext::RouteKey(const GuiKeyEvent& event) { return inputRouter_ && inputRouter_->OnKey(event); }
bool GuiContext::RouteChar(const GuiCharEvent& event) { return inputRouter_ && inputRouter_->OnChar(event); }
bool GuiContext::RouteMouseDown(const GuiMouseEvent& event) { return inputRouter_ && inputRouter_->OnMouseDown(event); }
bool GuiContext::RouteMouseUp(const GuiMouseEvent& event) { return inputRouter_ && inputRouter_->OnMouseUp(event); }
bool GuiContext::RouteMouseMove(const GuiMouseEvent& event) { return inputRouter_ && inputRouter_->OnMouseMove(event); }
bool GuiContext::RouteScroll(const GuiScrollEvent& event, int mouseX, int mouseY)
{
    return inputRouter_ && inputRouter_->OnScroll(event, mouseX, mouseY);
}

bool GuiContext::WantsCaptureMouse() const { return inputRouter_ && inputRouter_->WantsCaptureMouse(); }
bool GuiContext::WantsCaptureKeyboard() const { return inputRouter_ && inputRouter_->WantsCaptureKeyboard(); }

void GuiContext::ClearInputState()
{
    if (inputRouter_) {
        inputRouter_->ClearInteractionState();
    }
}

} // namespace cutum
