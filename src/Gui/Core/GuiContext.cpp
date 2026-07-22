#include "Gui/Core/GuiContext.h"
#include "Gui/Core/GuiInputRouter.h"
#include "Gui/Core/GuiRenderer.h"
#include "Gui/Core/GuiScale.h"
#include "Gui/Core/GuiScreenBase.h"
#include "Gui/Widgets/GuiWidget.h"
#include "Render/Engine/ShaderManager.h"
#include "Render/Engine/TextRenderer.h"

namespace cutum
{

UGuiContext::UGuiContext() = default;

UGuiContext::~UGuiContext() { Shutdown(); }

bool UGuiContext::Initialize(std::shared_ptr<UShaderManager> shaderManager,
                             std::shared_ptr<UTextRenderer> textRenderer)
{
  Theme = DefaultGuiTheme();
  BaseTheme = Theme;
  UiScale = 1.f;
  Metrics = MakeGuiMetrics(UiScale, Theme);
  Renderer = std::make_unique<UGuiRenderer>();
  InputRouter = std::make_unique<UGuiInputRouter>();
  InputRouter->SetRenderer(Renderer.get());
  return Renderer->Initialize(std::move(shaderManager),
                              std::move(textRenderer));
}

void UGuiContext::Shutdown()
{
  if (InputRouter)
  {
    InputRouter->ClearInteractionState();
    InputRouter->SetActiveScreen(nullptr);
  }
  if (ActiveScreen)
  {
    ActiveScreen->OnDetach();
    ActiveScreen.reset();
  }
  if (Renderer)
  {
    Renderer->Shutdown();
    Renderer.reset();
  }
  if (InputRouter)
  {
    InputRouter->ReleaseFocusWithoutNotify();
    InputRouter->SetRoot(nullptr);
  }
}

void UGuiContext::SetScreen(std::unique_ptr<UGuiScreenBase> screen)
{
  if (InputRouter)
  {
    InputRouter->ClearInteractionState();
  }
  if (ActiveScreen)
  {
    ActiveScreen->OnDetach();
  }
  ActiveScreen = std::move(screen);
  if (ActiveScreen)
  {
    ActiveScreen->OnAttach(*this);
    ActiveScreen->Build(*this);
    InputRouter->SetRoot(ActiveScreen->GetRoot());
    InputRouter->SetActiveScreen(ActiveScreen.get());
  }
  else
  {
    InputRouter->SetRoot(nullptr);
    InputRouter->SetActiveScreen(nullptr);
  }
}

void UGuiContext::Update(double dt)
{
  if (ActiveScreen)
  {
    ActiveScreen->Update(dt);
    if (ActiveScreen->GetRoot())
    {
      ActiveScreen->GetRoot()->Update(dt);
    }
  }
}

void UGuiContext::NotifyViewport(int WindowWidth, int WindowHeight,
                                 int insetLeft, int insetTop, int insetRight,
                                 int insetBottom)
{
  if (ActiveScreen)
  {
    ActiveScreen->SetViewportInsets(insetLeft, insetTop, insetRight,
                                    insetBottom);
    ActiveScreen->OnViewportChanged(WindowWidth, WindowHeight);
  }
}

void UGuiContext::Render(int WindowWidth, int WindowHeight, int insetLeft,
                         int insetTop, int insetRight, int insetBottom)
{
  if (!Renderer || !ActiveScreen || !ActiveScreen->GetRoot())
  {
    return;
  }
  NotifyViewport(WindowWidth, WindowHeight, insetLeft, insetTop, insetRight,
                 insetBottom);
  RenderOverlay(*ActiveScreen->GetRoot(), WindowWidth, WindowHeight);
}

void UGuiContext::RenderOverlay(UGuiWidget &root, int WindowWidth,
                                int WindowHeight, bool expandRootToViewport)
{
  if (!Renderer)
  {
    return;
  }
  Renderer->BeginFrame(WindowWidth, WindowHeight);
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
  root.Draw(*Renderer);
  Renderer->EndFrame();
}

bool UGuiContext::RouteKey(const GuiKeyEvent &event)
{
  return InputRouter && InputRouter->OnKey(event);
}
bool UGuiContext::RouteChar(const GuiCharEvent &event)
{
  return InputRouter && InputRouter->OnChar(event);
}
bool UGuiContext::RouteMouseDown(const GuiMouseEvent &event)
{
  return InputRouter && InputRouter->OnMouseDown(event);
}
bool UGuiContext::RouteMouseUp(const GuiMouseEvent &event)
{
  return InputRouter && InputRouter->OnMouseUp(event);
}
bool UGuiContext::RouteMouseMove(const GuiMouseEvent &event)
{
  return InputRouter && InputRouter->OnMouseMove(event);
}
bool UGuiContext::RouteScroll(const GuiScrollEvent &event, int mouseX,
                              int mouseY)
{
  return InputRouter && InputRouter->OnScroll(event, mouseX, mouseY);
}

bool UGuiContext::WantsCaptureMouse() const
{
  return InputRouter && InputRouter->WantsCaptureMouse();
}
bool UGuiContext::WantsCaptureKeyboard() const
{
  return InputRouter && InputRouter->WantsCaptureKeyboard();
}

void UGuiContext::ClearInputState()
{
  if (InputRouter)
  {
    InputRouter->ClearInteractionState();
  }
}

void UGuiContext::ApplyUiScale(float scale)
{
  UiScale = scale;
  Theme = ScaleGuiTheme(BaseTheme, UiScale);
  Metrics = MakeGuiMetrics(UiScale, Theme);
  if (Renderer)
  {
    Renderer->SetTextScale(UiScale);
  }
  NotifyMetricsChanged();
}

void UGuiContext::AddMetricsChangedListener(MetricsChangedFn listener)
{
  if (listener)
  {
    MetricsListeners.push_back(std::move(listener));
  }
}

void UGuiContext::ClearMetricsChangedListeners()
{
  MetricsListeners.clear();
}

void UGuiContext::NotifyMetricsChanged()
{
  if (ActiveScreen)
  {
    ActiveScreen->OnMetricsChanged(Metrics);
  }
  for (const MetricsChangedFn &listener : MetricsListeners)
  {
    if (listener)
    {
      listener(Metrics);
    }
  }
}

} // namespace cutum
