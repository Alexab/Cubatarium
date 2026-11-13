#include "Gui/Screens/CraftingScreen.h"
#include "Game/Survival/SurvivalSystems.h"
#include "Gui/Widgets/GuiTabBar.h"
#include "Gui/Widgets/GuiTextInput.h"
#include "Items/ItemDefinitionStorage.h"
#include <algorithm>
#include <cctype>
#include <cmath>

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

bool UCraftingScreen::IsSearchFocused() const
{
  return Search && Search->IsFocused();
}
bool UCraftingScreen::RouteTextKey(const GuiKeyEvent &e)
{
  return IsSearchFocused() ? Search->OnKey(e) : false;
}
bool UCraftingScreen::RouteTextChar(const GuiCharEvent &e)
{
  return IsSearchFocused() ? Search->OnChar(e) : false;
}
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

  auto search = std::make_unique<UGuiTextInput>(Theme);
  Search = search.get();
  Search->SetClipboard(ctx.GetClipboard());
  Search->SetOnEdited([this] { RebuildList(); });
  Panel->AddChild(std::move(search));
  auto tabs = std::make_unique<UGuiTabBar>(Theme);
  Categories = tabs.get();
  Categories->SetTabs(
      {"All", "Tools", "Armor", "Utility", "Food", "Materials", "Stations"});
  Categories->SetOnTabChanged(
      [this](int i)
      {
        Category = i;
        RebuildList();
      });
  Panel->AddChild(std::move(tabs));
  auto batch = std::make_unique<UGuiButton>(Theme, "Count: 1");
  BatchButton = batch.get();
  BatchButton->SetOnClick(
      [this]
      {
        if (!PendingRecipe.empty())
          return;
        Batch = Batch == 1 ? 8 : 1;
        BatchButton->SetLabel("Count: " + std::to_string(Batch));
        RebuildList();
      });
  Panel->AddChild(std::move(batch));
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
  if (!visible)
  {
    PendingRecipe.clear();
    CraftRemaining = 0;
  }
  if (Root)
  {
    Root->SetVisible(visible);
  }
  if (visible)
  {
    // UX U3: the recipe directory (12 JSONs) used to be re-read and
    // re-parsed on every open; load once per process.
    if (!RecipesLoaded)
    {
      Recipes.LoadFromDirectory("content/recipes");
      RecipesLoaded = true;
    }
    RebuildList();
    Relayout();
  }
}

void UCraftingScreen::Toggle() { SetVisible(!Visible); }

void UCraftingScreen::Update(double dt)
{
  if (!PendingRecipe.empty() && World)
  {
    auto *actor = World->GetControlledCreature();
    const auto *recipe = Recipes.FindById(PendingRecipe);
    if (!actor || actor->GetId() != CraftOwner || !recipe)
    {
      PendingRecipe.clear();
      CraftRemaining = 0;
    }
    else
    {
      CraftRemaining -= float(dt);
      StatusText = "Processing: " +
                   std::to_string(std::max(0, int(std::ceil(CraftRemaining)))) +
                   " s (close to cancel)";
      if (CraftRemaining <= 0)
      {
        auto trial = actor->GetInventory();
        bool ok = true;
        std::string reason;
        for (int i = 0; i < Batch && ok; ++i)
          ok = Recipes.TryCraft(
              trial, *recipe,
              USurvivalSystems::Instance().NearStation(
                  *World, actor->GetBodyOrigin(), recipe->Station),
              &reason);
        if (ok)
        {
          actor->GetInventory() = std::move(trial);
          StatusText = "Crafted " +
                       std::to_string(recipe->Output.Count * Batch) + " x " +
                       recipe->Output.Id;
        }
        else
          StatusText = reason;
        PendingRecipe.clear();
        RebuildList();
      }
    }
  }

  if (Status && Status->GetText() != StatusText)
  {
    Status->SetText(StatusText);
  }
  if (Visible && Icons)
  {
    if (Icons->PendingItemIcons() > 0)
    {
      Icons->WarmupItemIcons(4);
    }
    RefreshRecipeIcons();
  }
}

void UCraftingScreen::Relayout()
{
  if (!Panel || !Theme)
  {
    return;
  }
  const int w = std::min(ViewportW - 16, Scaled(940));
  const int h = std::min(ViewportH - 16, Scaled(650));
  const int x = (ViewportW - w) / 2;
  const int y = (ViewportH - h) / 2;
  Panel->SetBounds({x, y, w, h});
  if (Title)
  {
    Title->SetBounds({x + 16, y + 12, w - 32, 28});
  }
  Search->SetBounds({x + 12, y + 44, w - Scaled(160) - 36, Scaled(30)});
  BatchButton->SetBounds(
      {x + w - Scaled(160) - 12, y + 44, Scaled(160), Scaled(30)});
  Categories->SetBounds({x + 12, y + 48 + Scaled(30), w - 24, Scaled(30)});
  if (Status)
    Status->SetBounds({x + 12, y + 52 + Scaled(60), w - 24, Scaled(24)});
  if (Scroll)
  {
    Scroll->SetBounds(
        {x + 12, y + 60 + Scaled(84), w - 24, h - 76 - Scaled(84)});
  }
}

void UCraftingScreen::RebuildList()
{
  if (!Scroll || !Theme)
  {
    return;
  }
  Scroll->Content().ClearChildren();
  RecipeIconSlots.clear();
  RecipeOutputIds.clear();
  const int slotSize = Theme->HotbarSlotSize;
  int y = 4;
  for (const RecipeDefinition &recipe : Recipes.GetRecipes())
  {
    const char *categories[] = {"All",  "Tools",     "Armor",   "Utility",
                                "Food", "Materials", "Stations"};
    if (Category > 0 && recipe.Category != categories[Category])
      continue;
    auto lower = [](std::string v)
    {
      std::replace(v.begin(), v.end(), '_', ' ');
      std::transform(v.begin(), v.end(), v.begin(),
                     [](unsigned char c) { return char(std::tolower(c)); });
      return v;
    };
    if (Search && !Search->GetText().empty() &&
        lower(recipe.Id).find(lower(Search->GetText())) == std::string::npos)
      continue;
    const int rowW = std::max(260, std::min(ViewportW - 40, Scaled(916)));
    auto row = std::make_unique<UGuiPanel>(Theme);
    row->SetDrawBackground(false);
    UGuiPanel *rowPtr = row.get();

    auto icon = std::make_unique<UGuiSlot>(Theme);
    icon->SetBounds({4, 4, slotSize, slotSize});
    if (Icons)
    {
      icon->SetIconTexture(
          (World && World->GetItemDefinitionStorage() &&
                   World->GetItemDefinitionStorage()->Get(recipe.Output.Id)
               ? Icons->GetItemIconTexture(recipe.Output.Id)
               : Icons->GetBlockIconTexture(recipe.Output.Id)));
    }
    if (recipe.Output.Count > 1)
    {
      icon->SetCornerHint("x" + std::to_string(recipe.Output.Count));
    }
    UGuiSlot *iconPtr =
        static_cast<UGuiSlot *>(rowPtr->AddChild(std::move(icon)));
    RecipeIconSlots.push_back(iconPtr);
    RecipeOutputIds.push_back(recipe.Output.Id);

    std::ostringstream label;
    label << recipe.Output.Count << " x " << recipe.Output.Id << " ["
          << (recipe.Station.empty() ? "Hand" : recipe.Station) << "]\n";
    for (size_t i = 0; i < recipe.Inputs.size(); ++i)
    {
      if (i > 0)
      {
        label << ", ";
      }
      label << recipe.Inputs[i].Count << "x " << recipe.Inputs[i].Id;
      if (!recipe.Inputs[i].Alternatives.empty())
      {
        label << " (";
        for (size_t n = 0; n < recipe.Inputs[i].Alternatives.size(); ++n)
        {
          if (n)
            label << "/";
          label << recipe.Inputs[i].Alternatives[n];
        }
        label << ")";
      }
    }

    bool available = false;
    std::string unavailable = "No controlled creature";
    if (World)
      if (auto *actor = World->GetControlledCreature())
      {
        auto trial = actor->GetInventory();
        available = true;
        for (int n = 0; n < Batch && available; ++n)
          available = Recipes.TryCraft(
              trial, recipe,
              USurvivalSystems::Instance().NearStation(
                  *World, actor->GetBodyOrigin(), recipe.Station),
              &unavailable);
      }
    label << "\n" << (available ? "Available" : unavailable);
    auto text = std::make_unique<UGuiLabel>(Theme, label.str());
    text->SetUseSecondaryColor(true);
    text->SetBounds(
        {slotSize + 12, 8, rowW - slotSize - Scaled(110), Scaled(76)});
    rowPtr->AddChild(std::move(text));

    const std::string recipeId = recipe.Id;
    auto craft =
        std::make_unique<UGuiButton>(Theme, available ? "Craft" : "Need");
    craft->SetBounds({rowW - Scaled(90), 8, Scaled(82), Scaled(32)});
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
          if (!PendingRecipe.empty())
          {
            StatusText = "A recipe is already processing";
            return;
          }
          auto *actor = World->GetControlledCreature();
          auto trial = actor->GetInventory();
          std::string reason;
          bool possible = true;
          for (int n = 0; n < Batch && possible; ++n)
            possible = Recipes.TryCraft(
                trial, *def,
                USurvivalSystems::Instance().NearStation(
                    *World, actor->GetBodyOrigin(), def->Station),
                &reason);
          if (!possible)
          {
            StatusText = reason;
            return;
          }
          PendingRecipe = recipeId;
          CraftOwner = actor->GetId();
          CraftRemaining =
              actor->GetInventory().IsCreative() ? 0 : def->Seconds * Batch;
          StatusText = "Processing " + recipeId;
          Relayout();
        });
    rowPtr->AddChild(std::move(craft));

    rowPtr->SetBounds({0, y, rowW, std::max(slotSize + 12, Scaled(88))});
    Scroll->Content().AddChild(std::move(row));
    y += std::max(slotSize + 16, Scaled(92));
  }
  Scroll->LayoutContent();
}

void UCraftingScreen::RefreshRecipeIcons()
{
  if (!Icons)
  {
    return;
  }
  for (size_t i = 0; i < RecipeIconSlots.size() && i < RecipeOutputIds.size();
       ++i)
  {
    UGuiSlot *slot = RecipeIconSlots[i];
    if (!slot || slot->GetIconTexture() != 0)
    {
      continue;
    }
    const GLuint tex = Icons->GetItemIconTexture(RecipeOutputIds[i]);
    if (tex != 0)
    {
      slot->SetIconTexture(tex);
    }
  }
}

} // namespace cutum
