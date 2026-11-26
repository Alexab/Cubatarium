#include "Gui/Preview/ContentPreviewDock.h"

#include "Core/Memory/CallbackStorageSource.h"
#include "Core/Memory/StlSourceStorage.h"
#include "Gui/Core/GuiTheme.h"
#include "Gui/Preview/ContentPreviewRenderer.h"
#include "Gui/Widgets/GuiButton.h"
#include "Gui/Widgets/GuiLabel.h"
#include "Gui/Widgets/GuiPanel.h"
#include "Gui/Widgets/GuiPreviewViewport.h"

#include "Render/Textures/GlTextureOwner.h"
#include <new>

namespace
{

constexpr float kPreviewWalkTimeScale = 0.8f;
constexpr double kPreviewInterval = 1.0 / 30.0;

bool IsAnimatedCreaturePreviewKind(cutum::ContentKind kind)
{
  return kind == cutum::ContentKind::UCreature ||
         kind == cutum::ContentKind::Skin;
}

} // namespace

namespace cutum
{

struct UContentPreviewDock::RotateAction
{
  UContentPreviewDock *owner;
  void operator()(float yaw, float pitch) const
  {
    owner->Yaw = yaw;
    owner->Pitch = pitch;
    owner->MarkRenderDirty();
  }
};

struct UContentPreviewDock::ChangeAction
{
  UContentPreviewDock *owner;
  void operator()() const
  {
    if (owner->OnChange)
      owner->OnChange();
  }
};

UContentPreviewDock::UContentPreviewDock(const GuiTheme *theme,
                                         UContentPreviewRenderer *renderer)
    : Theme(theme), Renderer(renderer)
{
  auto panel = std::make_unique<UGuiPanel>(theme);
  panel->SetDrawBackground(true);
  panel->ReserveChildren(5);
  Root = std::move(panel);
  PanelShell = Root.get();

  auto title = std::make_unique<UGuiLabel>(theme, "Preview");
  TitleLabel = title.get();

  auto placeholder = std::make_unique<UGuiLabel>(theme, "Select an item");
  PlaceholderLabel = placeholder.get();

  auto viewport = std::make_unique<UGuiPreviewViewport>(theme);
  Viewport = viewport.get();
  viewport->SetAngles(Yaw, Pitch);
  viewport->SetOnRotationChanged(RotateAction{this});

  auto hint = std::make_unique<UGuiLabel>(theme, "Drag to rotate");
  HintLabel = hint.get();

  auto changeBtn = std::make_unique<UGuiButton>(theme, "Change...");
  changeBtn->SetVisible(false);
  changeBtn->SetOnClick(ChangeAction{this});
  ChangeButton = changeBtn.get();

  PanelShell->AddChild(std::move(title));
  PanelShell->AddChild(std::move(placeholder));
  PanelShell->AddChild(std::move(viewport));
  PanelShell->AddChild(std::move(hint));
  PanelShell->AddChild(std::move(changeBtn));
  SyncVisibility();
}

std::unique_ptr<UGuiPanel> UContentPreviewDock::ReleasePanel()
{
  return std::move(Root);
}

ContentPreviewDockStorage UContentPreviewDock::InspectHostedStorage(
    const GuiTheme &theme, const UContentPreviewRenderer *renderer,
    const UGuiWidget &panel) const noexcept
{
  ContentPreviewDockStorage out;
  if (Root || &panel != PanelShell || typeid(panel) != typeid(UGuiPanel) ||
      Theme != &theme || Renderer != renderer || PanelShell->Theme != &theme)
    return out;

  const auto &children = panel.GetChildren();
  const auto matches = [&](std::size_t index, const auto *alias)
  {
    using Widget = std::remove_cv_t<std::remove_pointer_t<decltype(alias)>>;
    const auto *child = children[index].get();
    return child && child == alias && typeid(*child) == typeid(Widget) &&
           alias->Theme == &theme && child->GetChildren().empty();
  };
  if (children.size() != 5 || !matches(0, TitleLabel) || !matches(1, PlaceholderLabel) ||
      !matches(2, Viewport) || !matches(3, HintLabel) || !matches(4, ChangeButton) ||
      ChangeButton->IsVisible() != bool(OnChange) ||
      (Viewport->PreviewTexture && Viewport->PreviewTexture != PreviewTexture))
    return out;

  const bool profile = StlSourceStorage::SupportedProfile();
  const auto rotate = InspectKnownOwnerCallback<RotateAction>(Viewport->OnRotationChanged, this, profile);
  const auto change = InspectKnownOwnerCallback<ChangeAction>(ChangeButton->OnClick, this, profile);
  StlSourceStorage storage;
  storage.Add(sizeof(*this) + sizeof(UGuiPanel) + 3 * sizeof(UGuiLabel) +
              sizeof(UGuiPreviewViewport) + sizeof(UGuiButton));
  storage.Buffer(children.capacity(), sizeof(std::unique_ptr<UGuiWidget>));
  for (const auto &child : children)
    storage.Buffer(child->GetChildren().capacity(), sizeof(std::unique_ptr<UGuiWidget>));
  for (const auto *label : {TitleLabel, PlaceholderLabel, HintLabel})
    storage.Text(label->GetText());
  storage.Text(ChangeButton->GetLabel());
  storage.Text(EntryId);
  storage.Add(rotate.requests);
  storage.Add(change.requests);
  out.requested = storage.bytes.requested;
  out.widgets = 1 + children.size();
  out.texturePresent = PreviewTexture != 0;
  out.supported = storage.bytes.supported && rotate.present && rotate.supported &&
                  change.present && change.supported;
  return out;
}

UContentPreviewDock::~UContentPreviewDock()
{
  if (PreviewTexture != 0)
  {
    glDeleteTextures(1, &PreviewTexture);
    PreviewTexture = 0;
  }
}

void UContentPreviewDock::SetSelection(ContentKind kind, const std::string &id,
                                       const std::string &displayName)
{
  const bool changed = Kind != kind || EntryId != id;
  auto nextId = id;
  if (id.empty())
    TitleLabel->SetText("Preview");
  else
    TitleLabel->SetText(displayName);
  Kind = kind;
  EntryId.swap(nextId);
  HasSelection = !EntryId.empty();
  if (changed)
  {
    Yaw = 45.0f;
    Pitch = 32.0f;
    PreviewAnimTime = 0.f;
    Viewport->SetAngles(Yaw, Pitch);
    Viewport->SetPreviewTexture(0);
  }
  if (!HasSelection && PreviewTexture)
  {
    UGlTextureOwner retired(PreviewTexture);
    PreviewTexture = 0;
  }
  MarkRenderDirty();
  SyncVisibility();
  RenderIfDirty();
}

void UContentPreviewDock::ClearSelection()
{
  SetSelection(Kind, "", "");
}

void UContentPreviewDock::SetOnChange(std::function<void()> handler,
                                      const std::string &buttonLabel)
{
  if (ChangeButton)
  {
    ChangeButton->SetLabel(buttonLabel);
    const bool show = static_cast<bool>(handler);
    if (ChangeButton->IsVisible() != show)
    {
      ChangeButton->SetVisible(show);
      LayoutContents(PanelShell->GetBounds());
    }
  }
  OnChange = std::move(handler);
}

void UContentPreviewDock::MarkRenderDirty()
{
  RenderDirty = true;
  RenderRetryPending = false;
  RenderThrottle = 0.0;
}

void UContentPreviewDock::RenderIfDirty() try
{
  if (!RenderDirty || (RenderRetryPending && RenderThrottle < kPreviewInterval))
    return;
  RenderThrottle = 0.0;
  RenderPreviewIfNeeded();
}
catch (const std::bad_alloc &)
{
  // Keep the request dirty until the renderer can publish the replacement.
  RenderRetryPending = true;
}

void UContentPreviewDock::SyncVisibility()
{
  const bool showPreview =
      HasSelection && Renderer && Renderer->SupportsKind(Kind);
  if (PlaceholderLabel)
  {
    PlaceholderLabel->SetVisible(!showPreview);
  }
  if (Viewport)
  {
    Viewport->SetVisible(showPreview);
  }
  if (HintLabel)
  {
    HintLabel->SetVisible(showPreview);
  }
}

void UContentPreviewDock::RenderPreviewIfNeeded()
{
  if (!RenderDirty || !Viewport || !Renderer || !HasSelection ||
      !Renderer->SupportsKind(Kind))
  {
    return;
  }
  const int size = std::max(128, std::min(RenderSize, 512));
  const bool animateWalk = IsAnimatedCreaturePreviewKind(Kind);
  UGlTextureOwner replacement(Renderer->RenderUnique(Kind, EntryId, size, Yaw, Pitch,
                                                      PreviewAnimTime, animateWalk));
  if (!replacement.Get())
  {
    RenderRetryPending = true;
    return;
  }
  UGlTextureOwner retired(PreviewTexture);
  PreviewTexture = replacement.Release();
  Viewport->SetPreviewTexture(PreviewTexture);
  RenderDirty = false;
  RenderRetryPending = false;
}

void UContentPreviewDock::Relayout(const GuiRect &bounds)
{
  if (!PanelShell || !Theme)
  {
    return;
  }
  const auto previous = PanelShell->GetBounds();
  if (previous.X == bounds.X && previous.Y == bounds.Y &&
      previous.W == bounds.W && previous.H == bounds.H)
    return;
  PanelShell->SetBounds(bounds);
  LayoutContents(bounds);
}

void UContentPreviewDock::LayoutContents(const GuiRect &bounds)
{
  RenderSize = std::max(128, std::min(bounds.W, bounds.H) - Theme->Padding * 4);

  const int pad = Theme->Padding;
  const int tabH = Theme->TabBarHeight;
  int y = bounds.Y + pad;
  const int innerW = bounds.W - pad * 2;

  if (TitleLabel)
  {
    const int titleH = Theme->FontSizeBody + pad;
    TitleLabel->SetBounds({bounds.X + pad, y, innerW, titleH});
    y += titleH + pad / 2;
  }
  if (ChangeButton && ChangeButton->IsVisible())
  {
    ChangeButton->SetBounds({bounds.X + pad, y, innerW, tabH});
    y += tabH + pad;
  }
  if (PlaceholderLabel)
  {
    const int placeholderH = std::max(40, bounds.H - (y - bounds.Y) - pad);
    PlaceholderLabel->SetBounds({bounds.X + pad, y, innerW, placeholderH});
  }
  const int hintH = Theme->FontSizeBody + pad;
  const int viewportH =
      std::max(80, bounds.H - (y - bounds.Y) - hintH - pad * 2);
  if (Viewport)
  {
    Viewport->SetBounds({bounds.X + pad, y, innerW, viewportH});
  }
  if (HintLabel)
  {
    HintLabel->SetBounds(
        {bounds.X + pad, bounds.Y + bounds.H - hintH - pad, innerW, hintH});
  }
  MarkRenderDirty();
}

std::string UContentPreviewDock::GetViewStateKey() const
{
  return EntryId + "|" + std::to_string(Yaw) + "|" + std::to_string(Pitch);
}
GuiRect UContentPreviewDock::GetViewportBounds() const
{
  return Viewport ? Viewport->GetBounds() : GuiRect{};
}

void UContentPreviewDock::Update(double dt)
{
  if (HasSelection && Renderer && Renderer->SupportsKind(Kind) &&
      IsAnimatedCreaturePreviewKind(Kind))
  {
    PreviewAnimTime += static_cast<float>(dt) * kPreviewWalkTimeScale;
    RenderDirty = true;
  }

  RenderThrottle += dt;
  if (RenderDirty && RenderThrottle >= kPreviewInterval)
    RenderIfDirty();
}

} // namespace cutum
