#ifndef CONTENT_PREVIEW_DOCK_H
#define CONTENT_PREVIEW_DOCK_H

#include "Gui/Core/GuiTypes.h"
#include "Gui/Interfaces/IContentCatalog.h"
#include <functional>
#include <memory>
#include <string>

namespace cutum
{

class UContentPreviewRenderer;
class UGuiButton;
class UGuiLabel;
class UGuiPanel;
class UGuiPreviewViewport;
struct GuiTheme;

class UContentPreviewDock
{
public:
  UContentPreviewDock(const GuiTheme *theme, UContentPreviewRenderer *renderer);
  ~UContentPreviewDock();

  std::unique_ptr<UGuiPanel> ReleasePanel();

  void SetSelection(ContentKind kind, const std::string &id,
                    const std::string &displayName);
  void ClearSelection();
  void SetOnChange(std::function<void()> handler);
  void Relayout(const GuiRect &bounds);
  void Update(double dt);
  void RenderIfDirty();

private:
  void MarkRenderDirty();
  void RenderPreviewIfNeeded();
  void SyncVisibility();

  const GuiTheme *Theme{nullptr};
  UContentPreviewRenderer *Renderer{nullptr};
  std::unique_ptr<UGuiPanel> Root;
  UGuiPanel *PanelShell{nullptr};
  UGuiLabel *TitleLabel{nullptr};
  UGuiLabel *PlaceholderLabel{nullptr};
  UGuiLabel *HintLabel{nullptr};
  UGuiPreviewViewport *Viewport{nullptr};
  UGuiButton *ChangeButton{nullptr};

  ContentKind Kind{ContentKind::Block};
  std::string EntryId;
  std::string DisplayName;
  bool HasSelection{false};
  bool RenderDirty{true};
  float Yaw{45.0f};
  float Pitch{32.0f};
  int RenderSize{256};
  double RenderThrottle{0.0};
  std::function<void()> OnChange;
};

} // namespace cutum

#endif
