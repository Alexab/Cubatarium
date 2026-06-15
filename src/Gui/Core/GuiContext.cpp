#include "Gui/Core/GuiContext.h"
#include "Gui/Core/GuiScale.h"
#include "Gui/Widgets/GuiWidget.h"
#include "Gui/Core/GuiInputRouter.h"
#include "Gui/Core/GuiRenderer.h"
#include "Gui/Core/GuiScreenBase.h"
#include "Render/Engine/ShaderManager.h"
#include "Render/Engine/TextRenderer.h"

namespace cutum
{

UGuiContext::UGuiContext() = default;

UGuiContext::~UGuiContext() { Shutdown(); }

bool UGuiContext::Initialize(std::shared_ptr<UShaderManager> shaderManager,
                             std::shared_ptr<UTextRenderer> textRenderer)
{
  theme_ = DefaultGuiTheme();
  baseTheme_ = theme_;
  uiScale_ = 1.f;
  renderer_ = std::make_unique<UGuiRenderer>();
  inputRouter_ = std::make_unique<UGuiInputRouter>();
  return renderer_->Initialize(std::move(shaderManager),
                               std::move(textRenderer));
}

void UGuiContext::Shutdown()
{
  if (inputRouter_)
  {
    inputRouter_->ClearInteractionState();
    inputRouter_->SetActiveScreen(nullptr);
  }
  if (activeScreen_)
  {
    activeScreen_->OnDetach();
    activeScreen_.reset();
  }
  if (renderer_)
  {
    renderer_->Shutdown();
    renderer_.reset();
  }
  if (inputRouter_)
  {
    inputRouter_->ReleaseFocusWithoutNotify();
    inputRouter_->SetRoot(nullptr);
  }
}

void UGuiContext::SetScreen(std::unique_ptr<UGuiScreenBase> screen)
{
  if (inputRouter_)
  {
    inputRouter_->ClearInteractionState();
  }
  if (activeScreen_)
  {
    activeScreen_->OnDetach();
  }
  activeScreen_ = std::move(screen);
  if (activeScreen_)
  {
    activeScreen_->OnAttach(*this);
    activeScreen_->Build(*this);
    inputRouter_->SetRoot(activeScreen_->GetRoot());
    inputRouter_->SetActiveScreen(activeScreen_.get());
  }
  else
  {
    inputRouter_->SetRoot(nullptr);
    inputRouter_->SetActiveScreen(nullptr);
  }
}

void UGuiContext::Update(double dt)
{
  if (activeScreen_)
  {
    activeScreen_->Update(dt);
    if (activeScreen_->GetRoot())
    {
      activeScreen_->GetRoot()->Update(dt);
    }
  }
}

void UGuiContext::NotifyViewport(int WindowWidth, int WindowHeight,
                                 int insetLeft, int insetTop, int insetRight,
                                 int insetBottom)
{
  if (activeScreen_)
  {
    activeScreen_->SetViewportInsets(insetLeft, insetTop, insetRight,
                                     insetBottom);
    activeScreen_->OnViewportChanged(WindowWidth, WindowHeight);
  }
}

void UGuiContext::Render(int WindowWidth, int WindowHeight, int insetLeft,
                         int insetTop, int insetRight, int insetBottom)
{
  if (!renderer_ || !activeScreen_ || !activeScreen_->GetRoot())
  {
    return;
  }
  NotifyViewport(WindowWidth, WindowHeight, insetLeft, insetTop, insetRight,
                 insetBottom);
  RenderOverlay(*activeScreen_->GetRoot(), WindowWidth, WindowHeight);
}

void UGuiContext::RenderOverlay(UGuiWidget &root, int WindowWidth,
                                int WindowHeight, bool expandRootToViewport)
{
  if (!renderer_)
  {
    return;
  }
  renderer_->BeginFrame(WindowWidth, WindowHeight);
  if (expandRootToViewport)
  {
    NotifyViewport(WindowWidth, WindowHeight);
    const GuiRect full{0, 0, WindowWidth, WindowHeight};
    root.SetBounds(full);
    root.UpdateLayout(full);
  }
  else
  {
    root.UpdateLayout(root.GetBounds());
  }
  root.Draw(*renderer_);
  renderer_->EndFrame();
}

bool UGuiContext::RouteKey(const GuiKeyEvent &event)
{
  return inputRouter_ && inputRouter_->OnKey(event);
}
bool UGuiContext::RouteChar(const GuiCharEvent &event)
{
  return inputRouter_ && inputRouter_->OnChar(event);
}
bool UGuiContext::RouteMouseDown(const GuiMouseEvent &event)
{
  return inputRouter_ && inputRouter_->OnMouseDown(event);
}
bool UGuiContext::RouteMouseUp(const GuiMouseEvent &event)
{
  return inputRouter_ && inputRouter_->OnMouseUp(event);
}
bool UGuiContext::RouteMouseMove(const GuiMouseEvent &event)
{
  return inputRouter_ && inputRouter_->OnMouseMove(event);
}
bool UGuiContext::RouteScroll(const GuiScrollEvent &event, int mouseX,
                              int mouseY)
{
  return inputRouter_ && inputRouter_->OnScroll(event, mouseX, mouseY);
}

bool UGuiContext::WantsCaptureMouse() const
{
  return inputRouter_ && inputRouter_->WantsCaptureMouse();
}
bool UGuiContext::WantsCaptureKeyboard() const
{
  return inputRouter_ && inputRouter_->WantsCaptureKeyboard();
}

void UGuiContext::ClearInputState()
{
  if (inputRouter_)
  {
    inputRouter_->ClearInteractionState();
  }
}

void UGuiContext::ApplyUiScale(float scale)
{
  uiScale_ = scale;
  theme_ = ScaleGuiTheme(baseTheme_, uiScale_);
  if (renderer_)
  {
    renderer_->SetTextScale(uiScale_);
  }
}

} // namespace cutum
