#ifndef CRAFTING_SCREEN_H
#define CRAFTING_SCREEN_H

#include "Game/Crafting/RecipeRegistry.h"
#include "Gui/Core/GuiScreenBase.h"

#include <string>
#include <vector>

namespace cutum
{

class UGameSession;
class UWorld;
class IUGuiIconSource;
class UGuiScrollView;
class UGuiPanel;
class UGuiLabel;
class UGuiButton;
struct GuiTheme;

/// Shapeless recipe list + craft buttons (MVP crafting UI).
class UCraftingScreen : public UGuiScreenBase
{
public:
  UCraftingScreen(UGameSession *session, UWorld *world, IUGuiIconSource *icons);
  ~UCraftingScreen() override;

  void Build(UGuiContext &ctx) override;
  void Update(double dt) override;
  bool BlocksGameInput() const override { return Visible; }

  void SetVisible(bool visible);
  void Toggle();
  void SetWorld(UWorld *world) { World = world; }

  void OnViewportChanged(int width, int height) override;

private:
  void RebuildList();
  void Relayout();

  UGameSession *Session{nullptr};
  UWorld *World{nullptr};
  IUGuiIconSource *Icons{nullptr};
  URecipeRegistry Recipes;

  UGuiPanel *Panel{nullptr};
  UGuiLabel *Title{nullptr};
  UGuiLabel *Status{nullptr};
  UGuiScrollView *Scroll{nullptr};
  const GuiTheme *Theme{nullptr};
  bool Visible{false};
  std::string StatusText;
};

} // namespace cutum

#endif
