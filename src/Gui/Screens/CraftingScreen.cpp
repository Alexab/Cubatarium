#include "Gui/Screens/CraftingScreen.h"
#include "Core/Memory/CallbackStorageSource.h"
#include "Core/Memory/StlSourceStorage.h"
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

#include <iterator>
#include <memory>
#include <new>
#include <sstream>
#include <typeinfo>

namespace cutum
{

namespace
{
constexpr const char *CategoriesText[]{"All", "Tools", "Armor", "Utility",
                                        "Food", "Materials", "Stations"};
}

class UCraftingScreen::RecipeRow final : public UGuiPanel
{
public:
  RecipeRow(const GuiTheme *theme, const RecipeDefinition &recipe,
            int height, int slotSize, int buttonWidth)
      : UGuiPanel(theme), Recipe(recipe), Height(height), SlotSize(slotSize),
        ButtonWidth(buttonWidth)
  {
    SetDrawBackground(false);
    ReserveChildren(3);
  }

  int GetPreferredHeight() const override { return Height; }

  void SetBounds(const GuiRect &bounds) override
  {
    UGuiPanel::SetBounds(bounds);
    Icon->SetBounds({bounds.X + 4, bounds.Y + 4, SlotSize, SlotSize});
    Button->SetBounds({bounds.X + bounds.W - ButtonWidth - 8, bounds.Y + 8,
                       ButtonWidth, 32});
    Text->SetBounds({bounds.X + SlotSize + 12, bounds.Y + 8,
                     std::max(0, bounds.W - SlotSize - ButtonWidth - 28), Height - 12});
  }

  const RecipeDefinition &Recipe;
  UGuiSlot *Icon{};
  UGuiLabel *Text{};
  UGuiButton *Button{};
  bool ItemIcon{};
private:
  int Height, SlotSize, ButtonWidth;
};

struct UCraftingScreen::RecipeAction
{
  UCraftingScreen *owner;
  const RecipeDefinition *Recipe;
  void operator()() const { owner->StartRecipe(*Recipe); }
};

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
  const auto &theme = ctx.GetTheme();
  auto panel = std::make_unique<UGuiPanel>(&theme);
  panel->SetDrawBackground(true);
  panel->SetZOrder(20);
  panel->SetVisible(Visible);
  panel->ReserveChildren(6);

  auto title = std::make_unique<UGuiLabel>(&theme, "Crafting");
  auto *nextTitle = title.get();
  panel->AddChild(std::move(title));

  auto search = std::make_unique<UGuiTextInput>(&theme);
  auto *nextSearch = search.get();
  search->SetClipboard(ctx.GetClipboard());
  if (Search)
    search->SetText(Search->GetText());
  search->SetOnEdited([this] { RowsDirty = true; RefreshList(); });
  panel->AddChild(std::move(search));

  auto tabs = std::make_unique<UGuiTabBar>(&theme);
  auto *nextCategories = tabs.get();
  tabs->SetTabs(std::vector<std::string>(std::begin(CategoriesText), std::end(CategoriesText)));
  tabs->SetActiveTab(Category);
  tabs->SetOnTabChanged([this](int i)
  {
    Category = i;
    RowsDirty = true;
    RefreshList();
  });
  panel->AddChild(std::move(tabs));

  auto batch = std::make_unique<UGuiButton>(&theme, Batch == 1 ? "Count: 1" : "Count: 8");
  auto *nextBatch = batch.get();
  batch->SetOnClick([this]
  {
    if (PendingRecipe)
      return;
    const int next = Batch == 1 ? 8 : 1;
    BatchButton->SetLabel(next == 1 ? "Count: 1" : "Count: 8");
    Batch = next;
    RowsDirty = true;
    RefreshList();
  });
  panel->AddChild(std::move(batch));

  auto status = std::make_unique<UGuiLabel>(&theme, StatusText);
  auto *nextStatus = status.get();
  status->SetUseSecondaryColor(true);
  panel->AddChild(std::move(status));

  auto *nextScroll = Scroll;
  if (Root)
    panel->Children.push_back(std::move(Panel->Children.back()));
  else
  {
    auto scroll = std::make_unique<UGuiScrollView>(&theme);
    nextScroll = scroll.get();
    panel->AddChild(std::move(scroll));
  }

  Theme = &theme;
  Panel = panel.get();
  Title = nextTitle;
  Search = nextSearch;
  Categories = nextCategories;
  BatchButton = nextBatch;
  Status = nextStatus;
  Scroll = nextScroll;
  Root = std::move(panel);
  Relayout();
}

void UCraftingScreen::OnViewportChanged(int width, int height)
{
  UGuiScreenBase::OnViewportChanged(width, height);
  Relayout();
}

void UCraftingScreen::SetWorld(UWorld *world)
{
  if (World != world)
  {
    World = world;
    if (PendingRecipe)
      StatusText.clear();
    PendingRecipe = nullptr;
    CraftRemaining = 0;
    RowsDirty = true;
  }
}

void UCraftingScreen::SetVisible(bool visible)
{
  if (!visible)
  {
    if (PendingRecipe)
      StatusText.clear();
    PendingRecipe = nullptr;
    CraftRemaining = 0;
    PointerPressed = false;
    if (Search)
      Search->SetFocused(false);
  }
  if (visible && !Visible)
    RowsDirty = true;
  Visible = visible;
  if (Root)
    Root->SetVisible(visible);
  if (visible)
  {
    Relayout();
    RefreshList();
  }
}

void UCraftingScreen::Toggle() { SetVisible(!Visible); }

void UCraftingScreen::Update(double dt) try
{
  if (PendingRecipe)
  {
    auto *actor = World ? World->GetControlledCreature() : nullptr;
    if (!actor || actor->GetId() != CraftOwner)
    {
      PendingRecipe = nullptr;
      CraftRemaining = 0;
      StatusText.clear();
    }
    else
    {
      CraftRemaining = std::max(0.f, CraftRemaining - float(dt));
      if (CraftRemaining <= 0)
      {
        const auto &recipe = *PendingRecipe;
        auto crafted = "Crafted " +
            std::to_string(static_cast<long long>(recipe.Output.Count) * Batch) +
            " x " + recipe.Output.Id;
        std::string reason;
        const bool ok = Recipes.TryCraftBatch(actor->GetInventory(), recipe, Batch,
            USurvivalSystems::Instance().NearStation(*World, actor->GetBodyOrigin(), recipe.Station),
            &reason);
        StatusText.swap(ok ? crafted : reason);
        PendingRecipe = nullptr;
        RowsDirty = true;
      }
      else
        StatusText = "Processing: " +
            std::to_string(int(std::ceil(CraftRemaining))) + " s (close to cancel)";
    }
  }

  RefreshList();
  if (Status && Status->GetText() != StatusText)
    Status->SetText(StatusText);
  if (Visible && Icons)
  {
    if (Icons->PendingItemIcons() > 0)
      Icons->WarmupItemIcons(4);
    RefreshRecipeIcons();
  }
}
catch (const std::bad_alloc &)
{
  // A refused inventory transaction stays pending, and published rows remain valid.
}

void UCraftingScreen::RefreshList() try
{
  if (!RowsDirty || !Visible || PointerPressed || !Scroll)
    return;
  if (!RecipesLoaded)
  {
    Recipes.LoadFromDirectory("content/recipes");
    RecipesLoaded = true;
  }
  RebuildList();
  RowsDirty = false;
}
catch (const std::bad_alloc &)
{
  // Keep the current row tree and retry on the next visible update.
}

void UCraftingScreen::StartRecipe(const RecipeDefinition &recipe) try
{
  if (!Visible)
    return;
  if (!World)
  {
    StatusText = "No world";
    return;
  }
  auto *actor = World->GetControlledCreature();
  if (!actor)
  {
    StatusText = "No controlled creature";
    return;
  }
  if (PendingRecipe)
  {
    StatusText = "A recipe is already processing";
    return;
  }
  std::string reason;
  auto &inventory = actor->GetInventory();
  if (!Recipes.CanCraftBatch(inventory, recipe, Batch,
      USurvivalSystems::Instance().NearStation(*World, actor->GetBodyOrigin(), recipe.Station),
      &reason))
  {
    StatusText.swap(reason);
    return;
  }
  StatusText = "Processing " + recipe.Id;
  CraftOwner = actor->GetId();
  CraftRemaining = inventory.IsCreative() ? 0 : recipe.Seconds * Batch;
  PendingRecipe = &recipe;
}
catch (const std::bad_alloc &)
{
  // No queued recipe has been published.
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
    const GuiRect next{x + 12, y + 60 + Scaled(84), w - 24, h - 76 - Scaled(84)};
    const auto previous = Scroll->GetBounds();
    Scroll->SetBounds(next);
    if (next.X != previous.X || next.Y != previous.Y ||
        next.W != previous.W || next.H != previous.H)
      Scroll->LayoutContent();
  }
}

void UCraftingScreen::RebuildList()
{
  std::vector<std::unique_ptr<UGuiWidget>> rows;
  const int slotSize = Theme->HotbarSlotSize;
  const int height = std::max(slotSize + 12, Scaled(88));
  const auto &query = Search->GetText();
  const auto fold = [](unsigned char c) { return c == '_' ? ' ' : std::tolower(c); };
  auto *actor = World ? World->GetControlledCreature() : nullptr;
  const auto *items = World ? World->GetItemDefinitionStorage() : nullptr;
  for (const RecipeDefinition &recipe : Recipes.GetRecipes())
  {
    if (Category > 0 && recipe.Category != CategoriesText[Category])
      continue;
    if (!query.empty() && std::search(recipe.Id.begin(), recipe.Id.end(), query.begin(), query.end(),
        [&](unsigned char a, unsigned char b) { return fold(a) == fold(b); }) == recipe.Id.end())
      continue;

    auto row = std::make_unique<RecipeRow>(Theme, recipe, height, slotSize, Scaled(82));
    auto icon = std::make_unique<UGuiSlot>(Theme);
    if (recipe.Output.Count > 1)
      icon->SetCornerHint("x" + std::to_string(recipe.Output.Count));
    row->ItemIcon = items && items->Get(recipe.Output.Id);
    row->Icon = icon.get();
    row->AddChild(std::move(icon));

    std::ostringstream label;
    label << recipe.Output.Count << " x " << recipe.Output.Id << " ["
          << (recipe.Station.empty() ? "Hand" : recipe.Station) << "]\n";
    for (size_t i = 0; i < recipe.Inputs.size(); ++i)
    {
      if (i)
        label << ", ";
      const auto &input = recipe.Inputs[i];
      label << input.Count << "x " << input.Id;
      if (!input.Alternatives.empty())
      {
        label << " (";
        for (size_t j = 0; j < input.Alternatives.size(); ++j)
        {
          if (j)
            label << "/";
          label << input.Alternatives[j];
        }
        label << ")";
      }
    }

    std::string unavailable = "No controlled creature";
    const bool available = actor && Recipes.CanCraftBatch(actor->GetInventory(), recipe, Batch,
        USurvivalSystems::Instance().NearStation(*World, actor->GetBodyOrigin(), recipe.Station),
        &unavailable);
    label << "\n" << (available ? "Available" : unavailable);
    auto text = std::make_unique<UGuiLabel>(Theme, label.str());
    text->SetUseSecondaryColor(true);
    row->Text = text.get();
    row->AddChild(std::move(text));

    auto craft = std::make_unique<UGuiButton>(Theme, available ? "Craft" : "Need");
    craft->SetOnClick(RecipeAction{this, &recipe});
    row->Button = craft.get();
    row->AddChild(std::move(craft));
    rows.push_back(std::move(row));
  }

  Scroll->Content().Children.swap(rows);
  Scroll->LayoutContent();
}

void UCraftingScreen::RefreshRecipeIcons()
{
  for (const auto &child : Scroll->Content().GetChildren())
  {
    auto &row = static_cast<RecipeRow &>(*child);
    if (!row.Icon->GetIconTexture())
      row.Icon->SetIconTexture(row.ItemIcon
          ? Icons->GetItemIconTexture(row.Recipe.Output.Id)
          : Icons->GetBlockIconTexture(row.Recipe.Output.Id));
  }
}

CraftingRowsStorage UCraftingScreen::InspectRowsStorage() const noexcept
{
  CraftingRowsStorage out;
  if (!Scroll || !Theme || !StlSourceStorage::SupportedProfile())
    return out;
  const auto &rows = Scroll->Content().GetChildren();
  const auto &recipes = Recipes.GetRecipes();
  StlSourceStorage widgets, children, text, callbacks, total;
  children.Buffer(rows.capacity(), sizeof(std::unique_ptr<UGuiWidget>));
  for (const auto &widget : rows)
  {
    if (!widget || typeid(*widget) != typeid(RecipeRow))
      return out;
    const auto &row = static_cast<const RecipeRow &>(*widget);
    const auto &kids = row.GetChildren();
    const auto knownRecipe = std::find_if(recipes.begin(), recipes.end(),
        [&](const auto &recipe) { return &recipe == &row.Recipe; });
    if (knownRecipe == recipes.end() || kids.size() != 3 || !row.Icon || !row.Text ||
        !row.Button || kids[0].get() != row.Icon || kids[1].get() != row.Text ||
        kids[2].get() != row.Button || typeid(*row.Icon) != typeid(UGuiSlot) ||
        typeid(*row.Text) != typeid(UGuiLabel) || typeid(*row.Button) != typeid(UGuiButton) ||
        !row.Icon->GetChildren().empty() || !row.Text->GetChildren().empty() ||
        !row.Button->GetChildren().empty() || row.Icon->OnClick || row.Icon->OnActivate ||
        row.Icon->OnBeginDrag)
      return out;
    const auto action = InspectKnownOwnerCallback<RecipeAction>(row.Button->OnClick, this, true);
    if (!action.present || !action.supported ||
        row.Button->OnClick.target<RecipeAction>()->Recipe != &row.Recipe)
      return out;

    widgets.Add(sizeof(RecipeRow) + sizeof(UGuiSlot) + sizeof(UGuiLabel) + sizeof(UGuiButton));
    children.Buffer(kids.capacity(), sizeof(std::unique_ptr<UGuiWidget>));
    for (const auto &kid : kids)
      children.Buffer(kid->GetChildren().capacity(), sizeof(std::unique_ptr<UGuiWidget>));
    text.Text(row.Icon->Label);
    text.Text(row.Icon->CornerHint);
    text.Text(row.Icon->CountHint);
    text.Text(row.Text->GetText());
    text.Text(row.Button->GetLabel());
    callbacks.Add(action.requests);
  }
  for (const auto *category : {&widgets, &children, &text, &callbacks})
  {
    total.Add(category->bytes.requested);
    total.bytes.supported &= category->bytes.supported;
  }
  out.rows = rows.size();
  out.widgetBytes = widgets.bytes.requested;
  out.childrenBytes = children.bytes.requested;
  out.textBytes = text.bytes.requested;
  out.callbackBytes = callbacks.bytes.requested;
  out.requested = total.bytes.requested;
  out.supported = total.bytes.supported;
  return out;
}

} // namespace cutum
