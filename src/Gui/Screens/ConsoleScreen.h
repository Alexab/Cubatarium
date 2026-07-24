#ifndef CONSOLE_SCREEN_H
#define CONSOLE_SCREEN_H

#include "Gui/Core/GuiScreenBase.h"
#include "Gui/Core/GuiTheme.h"
#include "Gui/Core/GuiTypes.h"
#include <memory>
#include <string>

namespace cutum
{

class UGameSession;
class UGuiContext;
class UGuiListView;
class UGuiPopupMenu;
class UGuiRenderer;
class UGuiTextInput;

class UConsoleScreen : public UGuiScreenBase
{
public:
  explicit UConsoleScreen(UGameSession *session);

  void Build(UGuiContext &ctx) override;
  void Update(double dt) override;
  void OnViewportChanged(int width, int height) override;
  bool BlocksGameInput() const override { return Visible; }

  void SetVisible(bool visible);
  bool IsVisible() const { return Visible; }
  void SetKeyboardInsetBottom(int bottom);
  void Toggle();
  void SubmitCommand();
  void AttachPopup(UGuiPopupMenu *popup);

  bool RouteKey(const GuiKeyEvent &event);
  bool RouteChar(const GuiCharEvent &event);
  bool RouteMouseButton(const GuiMouseEvent &event, UGuiRenderer &renderer);
  bool RouteMouseMove(const GuiMouseEvent &event, UGuiRenderer &renderer);
  bool IsPopupOpen() const;

private:
  void Relayout();
  void OnInputEdited();
  bool HandleHistoryNavigation(const GuiKeyEvent &event);
  void OpenContextMenu(int x, int y);

  UGameSession *Session{nullptr};
  UGuiListView *LogView{nullptr};
  UGuiTextInput *Input{nullptr};
  UGuiPopupMenu *Popup{nullptr};
  GuiTheme ConsoleTheme{};
  bool Visible{false};
  int KeyboardInsetBottom{0};
  int HistoryBrowseFromEnd{-1};
  std::string DraftLine;
  bool DraftValid{false};
};

} // namespace cutum

#endif
