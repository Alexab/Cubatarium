#ifndef ANVIL_SCREEN_H
#define ANVIL_SCREEN_H

#include "Gui/Core/GuiScreenBase.h"

#include <cstdint>
#include <optional>
#include <string>

namespace cutum
{

class UWorld;
class IUGuiIconSource;
class UGuiPanel;
class UGuiLabel;
class UGuiButton;
class UGuiSlot;
class UItemDefinitionStorage;
struct GuiTheme;

struct AnvilScreenStorage
{
  size_t screenBytes{}, widgets{}, widgetBytes{}, childrenBytes{}, textBytes{},
      callbackBytes{}, requested{};
  bool supported{}, pending{};
};

/// Repair UI for the active hotbar item (uses item repair materials).
class UAnvilScreen : public UGuiScreenBase
{
public:
  UAnvilScreen(UWorld *world, IUGuiIconSource *icons);
  ~UAnvilScreen() override;

  void Build(UGuiContext &ctx) override;
  void Update(double dt) override;
  bool BlocksGameInput() const override { return Visible; }

  void SetVisible(bool visible);
  void Toggle();
  void SetWorld(UWorld *world);

  void OnViewportChanged(int width, int height) override;
  AnvilScreenStorage InspectOwnedStorage(const UGuiContext &context, const UWorld *world,
                                         const IUGuiIconSource *icons) const noexcept;

private:
  friend struct AnvilStorageProbe;
  struct RepairAction;
  struct RepairRequest
  {
    uint64_t Actor{}, Instance{};
    size_t Row{}, Slot{};
  };
  enum class ItemDisplay { Initial, NoWorld, NoActor, NoItem, Item };
  void Refresh();
  void Relayout();
  void RequestRepair();
  void FinishRepair();

  UWorld *World{nullptr};
  IUGuiIconSource *Icons{nullptr};

  UGuiPanel *Panel{nullptr};
  UGuiLabel *Title{nullptr};
  UGuiLabel *ItemLabel{nullptr};
  UGuiLabel *Status{nullptr};
  UGuiSlot *ItemSlot{nullptr};
  UGuiButton *RepairBtn{nullptr};
  const GuiTheme *Theme{nullptr};
  bool Visible{false};
  std::optional<RepairRequest> PendingRepair;
  ItemDisplay Display{ItemDisplay::Initial};
  std::string DisplayedId;
  const UItemDefinitionStorage *DisplayedDefinitions{};
  uint64_t DisplayedRevision{};
  float DisplayedWear{};
  bool DisplayedBroken{};
};

} // namespace cutum

#endif
