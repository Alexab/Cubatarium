#include "Gui/Screens/CraftingScreen.h"

#include "Creatures/Core/Creature.h"
#include "Game/GameSession.h"
#include "Gui/Core/GuiContext.h"
#include "Gui/Core/GuiTheme.h"
#include "Gui/Interfaces/IUGuiIconSource.h"
#include "Gui/Widgets/GuiButton.h"
#include "Gui/Widgets/GuiLabel.h"
#include "Gui/Widgets/GuiPanel.h"
#include "Gui/Widgets/GuiScrollView.h"
#include "Gui/Widgets/GuiSlot.h"
#include "World/Core/World.h"

#include <memory>
#include <sstream>

namespace cutum
{

UCraftingScreen::UCraftingScreen(UGameSession *session, UWorld *world,
                                 IUGuiIconSource *icons)
    : Session(session), World(world), Icons(icons)
{
}

UCraftingScreen::~UCraftingScreen() = default;

void UCraftingScreen::Build(UGuiContext &ctx)
{
  Theme = &ctx.GetTheme();
  Recipes.LoadFromDirectory("content/recipes");

  auto panel = std::make_unique<UGuiPanel>(Theme);
  panel->SetDrawBackground(true);
  panel->SetZOrder(20);
  Panel = panel.get();

  auto title = std::make_unique<UGuiLabel>(Theme, "Crafting");
  Title = title.get();
  Panel->AddChild(std::move(title));

  auto status = std::make_unique<UGuiLabel>(Theme, "");
  status->SetUseSecondaryColor(true);
  Status = status.get();
  Panel->AddChild(std::move(status));

  auto scroll = std::make_unique<UGuiScrollView>(Theme);
  Scroll = scroll.get();
  Panel->AddChild(std::move(scroll));

  Root = std::move(panel);
  SetVisible(false);
}

void UCraftingScreen::OnViewportChanged(int width, int height)
{
  UGuiScreenBase::OnViewportChanged(width, height);
  Relayout();
}

void UCraftingScreen::SetVisible(bool visible)
{
  Visible = visible;
  if (Root)
  {
    Root->SetVisible(visible);
  }
  if (visible)
  {
    Recipes.LoadFromDirectory("content/recipes");
    RebuildList();
    Relayout();
  }
}

void UCraftingScreen::Toggle() { SetVisible(!Visible); }

void UCraftingScreen::Update(double /*dt*/)
{
  if (Status && Status->GetText() != StatusText)
  {
    Status->SetText(StatusText);
  }
}

void UCraftingScreen::Relayout()
{
  if (!Panel || !Theme)
  {
    return;
  }
  const int w = 480;
  const int h = 420;
  const int x = (ViewportW - w) / 2;
  const int y = (ViewportH - h) / 2;
  Panel->SetBounds({x, y, w, h});
  if (Title)
  {
    Title->SetBounds({x + 16, y + 12, w - 32, 28});
  }
  if (Status)
  {
    Status->SetBounds({x + 16, y + 44, w - 32, 22});
  }
  if (Scroll)
  {
    Scroll->SetBounds({x + 12, y + 72, w - 24, h - 88});
  }
}

void UCraftingScreen::RebuildList()
{
  if (!Scroll || !Theme)
  {
    return;
  }
  Scroll->Content().ClearChildren();
  const int slotSize = Theme->HotbarSlotSize;
  int y = 4;
  for (const RecipeDefinition &recipe : Recipes.GetRecipes())
  {
    auto row = std::make_unique<UGuiPanel>(Theme);
    row->SetDrawBackground(false);
    UGuiPanel *rowPtr = row.get();

    auto icon = std::make_unique<UGuiSlot>(Theme);
    icon->SetBounds({4, 4, slotSize, slotSize});
    if (Icons)
    {
      icon->SetIconTexture(Icons->GetItemIconTexture(recipe.Output.Id));
    }
    if (recipe.Output.Count > 1)
    {
      icon->SetCornerHint("x" + std::to_string(recipe.Output.Count));
    }
    rowPtr->AddChild(std::move(icon));

    std::ostringstream label;
    label << recipe.Id << "  (";
    for (size_t i = 0; i < recipe.Inputs.size(); ++i)
    {
      if (i > 0)
      {
        label << ", ";
      }
      label << recipe.Inputs[i].Count << "x " << recipe.Inputs[i].Id;
    }
    label << ")";
    auto text = std::make_unique<UGuiLabel>(Theme, label.str());
    text->SetUseSecondaryColor(true);
    text->SetBounds({slotSize + 12, 8, 260, 28});
    rowPtr->AddChild(std::move(text));

    const std::string recipeId = recipe.Id;
    auto craft = std::make_unique<UGuiButton>(Theme, "Craft");
    craft->SetBounds({380, 8, 72, 32});
    craft->SetOnClick(
        [this, recipeId]()
        {
          if (!World)
          {
            StatusText = "No world";
            return;
          }
          UCreature *creature = World->GetControlledCreature();
          if (!creature)
          {
            StatusText = "No controlled creature";
            return;
          }
          const RecipeDefinition *def = Recipes.FindById(recipeId);
          if (!def)
          {
            StatusText = "Unknown recipe";
            return;
          }
          if (!Recipes.TryCraft(creature->GetInventory(), *def))
          {
            StatusText = "Missing ingredients";
            return;
          }
          StatusText = "Crafted " + def->Output.Id;
          RebuildList();
          Relayout();
        });
    rowPtr->AddChild(std::move(craft));

    rowPtr->SetBounds({0, y, 460, slotSize + 12});
    Scroll->Content().AddChild(std::move(row));
    y += slotSize + 16;
  }
  Scroll->LayoutContent();
}

} // namespace cutum
