#ifndef CRAFTING_SCREEN_H
#define CRAFTING_SCREEN_H

#include "Game/Crafting/RecipeRegistry.h"
#include "Gui/Core/GuiScreenBase.h"
#include "Gui/Core/GuiTypes.h"

#include <string>

namespace cutum
{

class UGameSession;
class UWorld;
class IUGuiIconSource;
class UGuiScrollView;
class UGuiPanel;
class UGuiLabel;
class UGuiButton;
class UGuiSlot;
class UGuiTextInput;
class UGuiTabBar;
struct GuiTheme;

struct CraftingRowsStorage
{
  size_t rows{}, widgetBytes{}, childrenBytes{}, textBytes{}, callbackBytes{}, requested{};
  bool supported{};
};

/// Shapeless recipe list + craft buttons (MVP crafting UI).
class UCraftingScreen : public UGuiScreenBase
{
public:
  UCraftingScreen(UGameSession *session, UWorld *world, IUGuiIconSource *icons);
  ~UCraftingScreen() override;

  bool RouteTextKey(const GuiKeyEvent &);
  bool RouteTextChar(const GuiCharEvent &);
  bool IsSearchFocused() const;
  void Build(UGuiContext &ctx) override;
  void Update(double dt) override;
  bool BlocksGameInput() const override { return Visible; }

  void SetVisible(bool visible);
  void Toggle();
  void SetWorld(UWorld *world);
  void SetPointerPressed(bool pressed) { PointerPressed = pressed; }

  void OnViewportChanged(int width, int height) override;
  CraftingRowsStorage InspectRowsStorage() const noexcept;

private:
  friend struct CraftingStorageProbe;
  class RecipeRow;
  struct RecipeAction;
  void RefreshList();
  void RebuildList();
  void StartRecipe(const RecipeDefinition &recipe);
  void Relayout();
  void RefreshRecipeIcons();

  UGameSession *Session{nullptr};
  UWorld *World{nullptr};
  IUGuiIconSource *Icons{nullptr};
  URecipeRegistry Recipes;
  bool RecipesLoaded{false};

  UGuiPanel *Panel{nullptr};
  UGuiTextInput *Search{nullptr};
  UGuiTabBar *Categories{nullptr};
  int Category{0}, Batch{1};
  UGuiButton *BatchButton{nullptr};
  const RecipeDefinition *PendingRecipe{nullptr};
  float CraftRemaining{0};
  uint64_t CraftOwner{0};
  UGuiLabel *Title{nullptr};
  UGuiLabel *Status{nullptr};
  UGuiScrollView *Scroll{nullptr};
  const GuiTheme *Theme{nullptr};
  bool Visible{false};
  bool RowsDirty{true};
  bool PointerPressed{false};
  std::string StatusText;
};

} // namespace cutum

#endif
