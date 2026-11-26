#include "Gui/Core/GuiContext.h"
#include "Core/Memory/StlSourceStorage.h"
#include "Gui/Core/GuiInputRouter.h"
#include "Gui/Core/GuiRenderer.h"
#include "Gui/Core/GuiScale.h"
#include "Gui/Core/GuiScreenBase.h"
#include "Gui/Widgets/GuiWidget.h"
#include "Render/Engine/ShaderManager.h"
#include "Render/Engine/TextRenderer.h"
#include <type_traits>

namespace cutum
{

UGuiContext::UGuiContext() = default;

UGuiContext::~UGuiContext() { Shutdown(); }

bool UGuiContext::Initialize(std::shared_ptr<UShaderManager> shaderManager,
                             std::shared_ptr<UTextRenderer> textRenderer)
{
  auto renderer = std::make_unique<UGuiRenderer>();
  auto router = std::make_unique<UGuiInputRouter>();
  if (!renderer->Initialize(std::move(shaderManager), std::move(textRenderer)))
    return false;

  UnbindScreenInput();
  if (InputRouter)
    InputRouter->SetRenderer(nullptr);
  Theme = DefaultGuiTheme();
  BaseTheme = Theme;
  UiScale = 1.f;
  Metrics = MakeGuiMetrics(UiScale, Theme);
  Renderer = std::move(renderer);
  InputRouter = std::move(router);
  InputRouter->SetRenderer(Renderer.get());
  BindScreenInput();
  return true;
}

void UGuiContext::Shutdown()
{
  UnbindScreenInput();
  if (InputRouter)
    InputRouter->SetRenderer(nullptr);
  if (ActiveScreen)
  {
    ActiveScreen->OnDetach();
    ActiveScreen.reset();
  }
  Renderer.reset();
}

void UGuiContext::UnbindScreenInput()
{
  if (InputRouter)
  {
    InputRouter->ClearInteractionState();
    InputRouter->SetRoot(nullptr);
    InputRouter->SetActiveScreen(nullptr);
  }
}

void UGuiContext::BindScreenInput()
{
  if (InputRouter && ActiveScreen)
  {
    InputRouter->SetRoot(ActiveScreen->GetRoot());
    InputRouter->SetActiveScreen(ActiveScreen.get());
  }
}

void UGuiContext::SetScreen(std::unique_ptr<UGuiScreenBase> screen)
{
  // The old widget tree can disappear before the next Build returns.
  UnbindScreenInput();
  if (ActiveScreen)
  {
    ActiveScreen->OnDetach();
  }
  ActiveScreen = std::move(screen);
  if (ActiveScreen)
  {
    try
    {
      ActiveScreen->OnAttach(*this);
      ActiveScreen->Build(*this);
    }
    catch (...)
    {
      auto failed = std::move(ActiveScreen);
      failed->OnDetach();
      throw;
    }
  }
  BindScreenInput();
}

GuiContextStorage UGuiContext::InspectOwnedStorage() const noexcept
{
  GuiContextStorage out;
  out.context_bytes = sizeof(*this);
  out.listeners = MetricsListeners.size();
  out.listener_capacity = MetricsListeners.capacity();
  StlSourceStorage storage;
  const auto request = [&storage](const auto &values) {
    const auto before = storage.bytes.requested;
    using Element = typename std::decay_t<decltype(values)>::value_type;
    storage.Buffer(values.capacity(), sizeof(Element));
    return storage.bytes.requested - before;
  };
  out.listener_bytes = request(MetricsListeners);
  const auto *root = ActiveScreen ? ActiveScreen->GetRoot() : nullptr;
  out.screen_attached = ActiveScreen && ActiveScreen->GetContext() == this;
  out.bindings_match = !ActiveScreen || out.screen_attached;
  if (InputRouter)
  {
    out.router_bytes = sizeof(UGuiInputRouter);
    out.focus_entries = InputRouter->FocusOrder.size();
    out.focus_bytes = request(InputRouter->FocusOrder);
    out.root_bound = root && InputRouter->Root == root;
    out.renderer_bound = Renderer && InputRouter->Renderer == Renderer.get();
    out.has_focus = InputRouter->KeyboardFocus != nullptr;
    out.bindings_match &= InputRouter->Root == root &&
                          InputRouter->Screen == ActiveScreen.get() &&
                          InputRouter->Renderer == Renderer.get();
  }
  else
    out.bindings_match &= !Renderer && !ActiveScreen;
  if (Renderer)
  {
    out.renderer_bytes = sizeof(UGuiRenderer);
    out.clip_bytes = request(Renderer->ClipStack);
    out.vertex_bytes = request(Renderer->QuadBatch.Vertices);
  }
  // External type-erased listeners can own arbitrary backing.
  out.supported = storage.bytes.supported && out.bindings_match && MetricsListeners.empty();
  return out;
}

void UGuiContext::InspectGpuBuffers(TerrainGpuBufferCensus &out) const
{
  if (Renderer)
    Renderer->InspectGpuBuffers(out);
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
