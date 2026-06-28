#include "Gui/Preview/ContentPreviewDock.h"

#include "Gui/Preview/ContentPreviewRenderer.h"
#include "Gui/Widgets/GuiButton.h"
#include "Gui/Widgets/GuiLabel.h"
#include "Gui/Widgets/GuiPanel.h"
#include "Gui/Widgets/GuiPreviewViewport.h"
#include "Gui/Core/GuiTheme.h"

namespace cutum
{

UContentPreviewDock::UContentPreviewDock(const GuiTheme *theme,
                                         UContentPreviewRenderer *renderer)
    : Theme(theme), Renderer(renderer)
{
  auto panel = std::make_unique<UGuiPanel>(theme);
  panel->SetDrawBackground(true);
  Root = std::move(panel);
  PanelShell = Root.get();

  auto title = std::make_unique<UGuiLabel>(theme, "Preview");
  TitleLabel = title.get();

  auto placeholder =
      std::make_unique<UGuiLabel>(theme, "Select an item");
  PlaceholderLabel = placeholder.get();

  auto viewport = std::make_unique<UGuiPreviewViewport>(theme);
  Viewport = viewport.get();
  viewport->SetAngles(Yaw, Pitch);
  viewport->SetOnRotationChanged(
      [this](float yaw, float pitch)
      {
        Yaw = yaw;
        Pitch = pitch;
        MarkRenderDirty();
      });

  auto hint = std::make_unique<UGuiLabel>(theme, "Drag to rotate");
  HintLabel = hint.get();

  auto changeBtn = std::make_unique<UGuiButton>(theme, "Change...");
  changeBtn->SetVisible(false);
  changeBtn->SetOnClick([this]() {
    if (OnChange)
    {
      OnChange();
    }
  });
  ChangeButton = changeBtn.get();

  PanelShell->AddChild(std::move(title));
  PanelShell->AddChild(std::move(placeholder));
  PanelShell->AddChild(std::move(viewport));
  PanelShell->AddChild(std::move(hint));
  PanelShell->AddChild(std::move(changeBtn));
}

std::unique_ptr<UGuiPanel> UContentPreviewDock::ReleasePanel()
{
  return std::move(Root);
}

UContentPreviewDock::~UContentPreviewDock() = default;

void UContentPreviewDock::SetSelection(ContentKind kind, const std::string &id,
                                       const std::string &displayName)
{
  Kind = kind;
  EntryId = id;
  DisplayName = displayName;
  HasSelection = !id.empty();
  Yaw = 45.0f;
  Pitch = 32.0f;
  if (Viewport)
  {
    Viewport->SetAngles(Yaw, Pitch);
  }
  if (TitleLabel)
  {
    TitleLabel->SetText(HasSelection ? displayName : "Preview");
  }
  MarkRenderDirty();
  SyncVisibility();
  RenderIfDirty();
}

void UContentPreviewDock::ClearSelection()
{
  SetSelection(Kind, "", "");
  HasSelection = false;
  SyncVisibility();
}

void UContentPreviewDock::SetOnChange(std::function<void()> handler,
                                     const std::string &buttonLabel)
{
  OnChange = std::move(handler);
  if (ChangeButton)
  {
    ChangeButton->SetLabel(buttonLabel);
    ChangeButton->SetVisible(static_cast<bool>(OnChange));
  }
}

void UContentPreviewDock::MarkRenderDirty()
{
  RenderDirty = true;
  RenderThrottle = 0.0;
}

void UContentPreviewDock::RenderIfDirty()
{
  RenderPreviewIfNeeded();
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
  const GLuint tex =
      Renderer->Render(Kind, EntryId, size, Yaw, Pitch);
  Viewport->SetPreviewTexture(tex);
  RenderDirty = false;
}

void UContentPreviewDock::Relayout(const GuiRect &bounds)
{
  if (!PanelShell || !Theme)
  {
    return;
  }
  PanelShell->SetBounds(bounds);
  RenderSize = std::max(128, std::min(bounds.W, bounds.H) - Theme->Padding * 4);

  const int pad = Theme->Padding;
  const int tabH = Theme->TabBarHeight;
  int y = bounds.Y + pad;
  const int innerW = bounds.W - pad * 2;

  if (TitleLabel)
  {
    TitleLabel->SetBounds({bounds.X + pad, y, innerW, 24});
    y += 28;
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

void UContentPreviewDock::Update(double dt)
{
  RenderThrottle += dt;
  if (RenderDirty && RenderThrottle >= 1.0 / 30.0)
  {
    RenderPreviewIfNeeded();
    RenderThrottle = 0.0;
  }
}

} // namespace cutum
