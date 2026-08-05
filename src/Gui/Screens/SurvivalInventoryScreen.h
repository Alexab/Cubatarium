#ifndef SURVIVAL_INVENTORY_SCREEN_H
#define SURVIVAL_INVENTORY_SCREEN_H

#include "Gui/Core/GuiScreenBase.h"

#include <string>
#include <vector>

namespace cutum
{

class IUContentCatalog;
class UGameSession;
class IUGuiIconSource;
class UGuiScrollView;
class UGuiPanel;
class UGuiSlot;
struct GuiTheme;

/// Survival backpack UI (owned items only).
class USurvivalInventoryScreen : public UGuiScreenBase
{
public:
  USurvivalInventoryScreen(IUContentCatalog *catalog, UGameSession *session,
                            IUGuiIconSource *icons);
  ~USurvivalInventoryScreen();

  void Build(UGuiContext &ctx) override;
  void Update(double dt) override;
  bool BlocksGameInput() const override { return Visible; }

  void SetVisible(bool visible);
  void Toggle();

  void OnViewportChanged(int width, int height) override;

private:
  void RebuildGrid();
  void LayoutGridInScroll();

  IUContentCatalog *Catalog{nullptr};
  UGameSession *Session{nullptr};
  IUGuiIconSource *Icons{nullptr};

  UGuiPanel *RootShell{nullptr};
  UGuiPanel *Panel{nullptr};
  UGuiScrollView *Scroll{nullptr};

  const GuiTheme *Theme{nullptr};
  bool Visible{false};
  bool Built{false};

  std::vector<UGuiSlot *> GridSlots;
  std::vector<std::string> GridEntryIds;
};

} // namespace cutum

#endif

