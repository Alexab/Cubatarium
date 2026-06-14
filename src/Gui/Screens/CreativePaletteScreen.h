#ifndef CREATIVE_PALETTE_SCREEN_H
#define CREATIVE_PALETTE_SCREEN_H

#include "Gui/GuiScreenBase.h"
#include "Gui/Interfaces/IContentCatalog.h"
#include "SlotInteraction.h"
#include <memory>
#include <string>

namespace cutum
{

class IContentCatalog;
class UGameSession;
class IGuiIconSource;
class UGuiTabBar;
class UGuiScrollView;
class UGuiPanel;
class UGuiSlot;
struct GuiTheme;

class UCreativePaletteScreen : public UGuiScreenBase
{
public:
  UCreativePaletteScreen(IContentCatalog *catalog, UGameSession *session,
                         IGuiIconSource *icons);

  bool PickSlot(int x, int y, SlotAddress &out) const;

  void OnViewportChanged(int width, int height) override;

  void Build(UGuiContext &ctx) override;
  void Update(double dt) override;
  bool BlocksGameInput() const override { return visible_; }

  void SetVisible(bool visible);
  void Toggle();
  void InvalidateGrid() { built_ = false; }

private:
  void RebuildGrid();
  void RelayoutPanel();
  void LayoutGridInScroll();

  IContentCatalog *catalog_{nullptr};
  UGameSession *session_{nullptr};
  IGuiIconSource *icons_{nullptr};
  std::vector<std::string> gridEntryIds_;
  UGuiPanel *panel_{nullptr};
  UGuiTabBar *mainTabs_{nullptr};
  UGuiTabBar *subTabs_{nullptr};
  UGuiScrollView *scroll_{nullptr};
  std::vector<UGuiSlot *> gridSlots_;
  std::string selectedEntryId_;
  ContentKind kind_{ContentKind::Block};
  std::string activeTypeId_;
  const GuiTheme *theme_{nullptr};
  bool visible_{false};
  bool built_{false};
};

} // namespace cutum

#endif
