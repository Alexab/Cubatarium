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

struct ConsoleStorage
{
  size_t screen_bytes{}, widget_bytes{}, children_bytes{}, input_text{}, draft_text{};
  size_t log_rows{}, log_slots{}, log_text{}, history_rows{}, history_slots{}, history_text{}, history_path{};
  size_t popup_bytes{}, popup_entries{}, popup_slots{}, popup_text{}, callback_bytes{}, requested{};
  bool present{}, visible{}, popup_open{}, borrowed_log{}, bindings_match{}, supported{};
};

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
  ConsoleStorage InspectOwnedStorage(const UGuiContext &context, const UGameSession *session,
                                       const UGuiPopupMenu *popup) const noexcept;

private:
  struct EditedAction;
  template<void (UGuiTextInput::*Edit)()> struct PopupEditAction;
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
