#include "Gui/Screens/PlayerWindowScreen.h"
#include "Core/Memory/CallbackStorageSource.h"
#include "Core/Memory/StlSourceStorage.h"
#include "Game/GameSession.h"
#include "Gui/Core/GuiContext.h"
#include "Gui/Interfaces/IUGuiIconSource.h"
#include "Gui/Screens/CharacterSheetScreen.h"
#include "Gui/Screens/CreativePaletteScreen.h"
#include "Gui/Widgets/GuiSlot.h"
#include "Gui/Widgets/GuiTabBar.h"
#include "Gui/Widgets/GuiWindow.h"
#include <algorithm>
#include <climits>
#include <typeinfo>
namespace cutum
{
// The pages keep their widget ownership and state. This host mounts the active
// page in one chrome/input tree without rebuilding or transferring its widgets.
class UPlayerSectionHost : public UGuiWidget
{
public:
  UGuiScreenBase *Page{nullptr};
  UGuiWidget *GetPageRoot() const { return Page ? Page->GetRoot() : nullptr; }
  void Draw(UGuiRenderer &r) override
  {
    if (auto *root = GetPageRoot())
      root->Draw(r);
  }
  UGuiWidget *HitTest(int x, int y) override
  {
    auto *root = GetPageRoot();
    return root && Bounds.Contains(x, y) ? root->HitTest(x, y) : nullptr;
  }
  UGuiWidget *HitTestFocusable(int x, int y) override
  {
    auto *root = GetPageRoot();
    return root && Bounds.Contains(x, y) ? root->HitTestFocusable(x, y)
                                         : nullptr;
  }
  bool ScrollAtPoint(int x, int y, const GuiScrollEvent &e) override
  {
    auto *root = GetPageRoot();
    return root && Bounds.Contains(x, y) && root->ScrollAtPoint(x, y, e);
  }
  void CollectFocusables(std::vector<UGuiWidget *> &out) override
  {
    if (auto *root = GetPageRoot())
      root->CollectFocusables(out);
  }
  bool OnMouseDown(const GuiMouseEvent &event) override
  {
    auto *root = GetPageRoot();
    return root && Bounds.Contains(event.X, event.Y) && root->OnMouseDown(event);
  }
  bool OnMouseUp(const GuiMouseEvent &event) override
  {
    auto *root = GetPageRoot();
    return root && root->OnMouseUp(event);
  }
  bool OnMouseMove(const GuiMouseEvent &event) override
  {
    auto *root = GetPageRoot();
    return root && root->OnMouseMove(event);
  }
  bool OnKey(const GuiKeyEvent &event) override
  {
    auto *root = GetPageRoot();
    return root && root->OnKey(event);
  }
  bool OnChar(const GuiCharEvent &event) override
  {
    auto *root = GetPageRoot();
    return root && root->OnChar(event);
  }
};
namespace
{
constexpr std::array<int, 5> Sections{0, 4, 1, 2, 3};

int TabForSection(int section)
{
  return static_cast<int>(std::find(Sections.begin(), Sections.end(), section) - Sections.begin());
}
}

struct UPlayerWindowScreen::TabAction
{
  UPlayerWindowScreen *owner;
  void operator()(int tab) const { owner->Session->OpenPlayerWindow(Sections[tab]); }
};

struct UPlayerWindowScreen::SlotClickAction
{
  UPlayerWindowScreen *owner;
  size_t slot;
  void operator()() const
  {
    auto &session = *owner->Session;
    const auto row = session.GetActiveHotbarRow();
    if (!session.ApplyPendingAssignment(row, slot))
      session.SelectSlot(row, slot);
  }
};

struct UPlayerWindowScreen::SlotDragAction
{
  UPlayerWindowScreen *owner;
  size_t slot;
  void operator()() const
  {
    auto &session = *owner->Session;
    const auto row = session.GetActiveHotbarRow();
    auto entry = session.GetHotbarEntryRef(row, slot);
    if (!entry.empty)
    {
      SlotAddress address;
      address.surface = SlotSurface::Hotbar;
      address.bar = row;
      address.slot = slot;
      session.BeginDragFromSlot(address, entry);
    }
  }
};

UPlayerWindowScreen::UPlayerWindowScreen(UGameSession *session,
                                         UCharacterSheetScreen *character,
                                         UCreativePaletteScreen *catalog,
                                         IUGuiIconSource *icons)
    : Session(session), Character(character), Catalog(catalog), Icons(icons)
{
}
void UPlayerWindowScreen::Build(UGuiContext &ctx)
{
  auto root = std::make_unique<UGuiPanel>(&ctx.GetTheme());
  root->SetDrawBackground(false);
  root->ReserveChildren(1);
  auto window = std::make_unique<UGuiWindow>(&ctx.GetTheme(), "Player");
  auto *nextWindow = window.get();
  window->ReserveChildren(2 + Slots.size());
  auto tabs = std::make_unique<UGuiTabBar>(&ctx.GetTheme());
  auto *nextTabs = tabs.get();
  tabs->SetTabs({"Backpack", "Catalog", "Equipment", "Status", "Appearance"});
  tabs->SetOnTabChanged(TabAction{this});
  window->AddChild(std::move(tabs));
  auto host = std::make_unique<UPlayerSectionHost>();
  auto *nextHost = host.get();
  window->AddChild(std::move(host));
  decltype(Slots) nextSlots{};
  for (size_t i = 0; i < nextSlots.size(); ++i)
  {
    auto slot = std::make_unique<UGuiSlot>(&ctx.GetTheme());
    nextSlots[i] = slot.get();
    slot->SetCornerHint(std::to_string((i + 1) % 10));
    slot->SetOnClick(SlotClickAction{this, i});
    slot->SetOnBeginDrag(SlotDragAction{this, i});
    window->AddChild(std::move(slot));
  }
  root->AddChild(std::move(window));
  if (!Root)
  {
    Character->SetHosted(true);
    Catalog->SetHosted(true);
    Character->SetVisible(true);
    Catalog->SetVisible(false);
    if (Character->GetSection() != Section)
      Character->SetSection(Section);
  }
  nextHost->Page = Section == 4 ? static_cast<UGuiScreenBase *>(Catalog) : Character;
  nextTabs->SetTabEnabled(1, Session->GetWorldGameMode() == WorldGameMode::Creative);
  nextTabs->SetActiveTab(TabForSection(Section));

  Root = std::move(root);
  Window = nextWindow;
  Tabs = nextTabs;
  Host = nextHost;
  Slots = nextSlots;
  Relayout();
}

PlayerWindowStorage UPlayerWindowScreen::InspectOwnedStorage(
    const UGuiContext &context, const UGameSession *session,
    const UCharacterSheetScreen *character, const UCreativePaletteScreen *catalog,
    const IUGuiIconSource *icons) const noexcept
{
  PlayerWindowStorage out;
  out.present = true;
  out.section = Section;
  if (typeid(*this) != typeid(UPlayerWindowScreen) || Context != &context ||
      Metrics.Theme != &context.GetTheme() || !session || Session != session || Icons != icons ||
      !character || Character != character || character->GetContext() != &context ||
      !catalog || Catalog != catalog || catalog->GetContext() != &context ||
      !Root || typeid(*Root) != typeid(UGuiPanel))
    return out;

  const auto &root = static_cast<const UGuiPanel &>(*Root);
  const auto &outer = root.GetChildren();
  const auto *theme = &context.GetTheme();
  if (root.Theme != theme || outer.size() != 1 || !outer[0] || outer[0].get() != Window ||
      typeid(*outer[0]) != typeid(UGuiWindow) || Window->Theme != theme)
    return out;
  const auto &children = Window->GetChildren();
  const auto *expectedPage = Section == 4 ? static_cast<const UGuiScreenBase *>(catalog) : character;
  if (children.size() != 2 + Slots.size() || !children[0] || children[0].get() != Tabs ||
      typeid(*children[0]) != typeid(UGuiTabBar) || Tabs->Theme != theme ||
      Tabs->Labels.size() != Sections.size() || Tabs->Enabled.size() != Sections.size() ||
      Tabs->GetActiveTab() != TabForSection(Section) || !children[1] || children[1].get() != Host ||
      typeid(*children[1]) != typeid(UPlayerSectionHost) || Host->Page != expectedPage ||
      !expectedPage->GetRoot() ||
      !Tabs->GetChildren().empty() || !Host->GetChildren().empty())
    return out;
  for (size_t i = 0; i < Slots.size(); ++i)
  {
    const auto &child = children[2 + i];
    if (!child || child.get() != Slots[i] || typeid(*child) != typeid(UGuiSlot) ||
        Slots[i]->Theme != theme || !child->GetChildren().empty())
      return out;
  }
  out.bindings_match = true;
  out.screen_bytes = sizeof(*this);
  out.widgets = 4 + Slots.size();
  out.widget_bytes = sizeof(UGuiPanel) + sizeof(UGuiWindow) + sizeof(UGuiTabBar) +
                     sizeof(UPlayerSectionHost) + Slots.size() * sizeof(UGuiSlot);

  StlSourceStorage childSlots, tabBacking, text, callbacks, total;
  const auto countChildren = [&childSlots](const UGuiWidget &widget) {
    childSlots.Buffer(widget.GetChildren().capacity(), sizeof(std::unique_ptr<UGuiWidget>));
  };
  countChildren(root);
  countChildren(*Window);
  for (const auto &child : children)
    countChildren(*child);
  tabBacking.Buffer(Tabs->Labels.capacity(), sizeof(std::string));
  // MSVC vector<bool> reports its allocated word capacity in bits.
  tabBacking.Buffer(Tabs->Enabled.capacity() / CHAR_BIT, 1);
  text.Text(Window->Title);
  for (const auto &label : Tabs->Labels)
    text.Text(label);
  const bool profile = StlSourceStorage::SupportedProfile();
  const auto tab = InspectKnownOwnerCallback<TabAction>(Tabs->OnTabChanged, this, profile);
  bool known = tab.supported && tab.present;
  out.callbacks = tab.present;
  callbacks.Add(tab.requests);
  for (size_t i = 0; i < Slots.size(); ++i)
  {
    const auto &slot = *Slots[i];
    text.Text(slot.Label);
    text.Text(slot.CornerHint);
    text.Text(slot.CountHint);
    const auto click = InspectKnownOwnerCallback<SlotClickAction>(slot.OnClick, this, profile);
    const auto drag = InspectKnownOwnerCallback<SlotDragAction>(slot.OnBeginDrag, this, profile);
    known &= click.supported && click.present && slot.OnClick.target<SlotClickAction>()->slot == i &&
             drag.supported && drag.present && slot.OnBeginDrag.target<SlotDragAction>()->slot == i &&
             !slot.OnActivate;
    out.callbacks += size_t(click.present) + size_t(drag.present) + size_t(bool(slot.OnActivate));
    callbacks.Add(click.requests);
    callbacks.Add(drag.requests);
  }
  out.children_bytes = childSlots.bytes.requested;
  out.tab_bytes = tabBacking.bytes.requested;
  out.text_bytes = text.bytes.requested;
  out.callback_bytes = callbacks.bytes.requested;
  for (const auto amount : {out.screen_bytes, out.widget_bytes, out.children_bytes, out.tab_bytes,
                            out.text_bytes, out.callback_bytes})
    total.Add(amount);
  out.requested = total.bytes.requested;
  out.supported = known && childSlots.bytes.supported && tabBacking.bytes.supported &&
                  text.bytes.supported && callbacks.bytes.supported && total.bytes.supported;
  return out;
}
void UPlayerWindowScreen::Relayout()
{
  if (!Window || !Metrics.Theme)
    return;
  Layout = PlayerWindowLayout::Compute(
      ViewportW, ViewportH, Metrics, GetContentOffsetX(), GetContentOffsetY());
  Root->SetBounds(
      {GetContentOffsetX(), GetContentOffsetY(), ViewportW, ViewportH});
  Window->SetBounds(Layout.Window);
  Tabs->SetBounds(Layout.Tabs);
  Host->SetBounds(Layout.Body);
  const int gap = Metrics.Theme->HotbarSlotGap;
  const int total = Layout.SlotSize * 10 + gap * 9;
  const int x = Layout.Footer.X + (Layout.Footer.W - total) / 2;
  for (size_t i = 0; i < Slots.size(); ++i)
    Slots[i]->SetBounds({x + int(i) * (Layout.SlotSize + gap), Layout.Footer.Y,
                         Layout.SlotSize, Layout.SlotSize});
}
void UPlayerWindowScreen::OnViewportChanged(int w, int h)
{
  UGuiScreenBase::OnViewportChanged(w, h);
  for (UGuiScreenBase *page : {static_cast<UGuiScreenBase *>(Character),
                               static_cast<UGuiScreenBase *>(Catalog)})
  {
    page->SetViewportInsets(GetContentOffsetX(), GetContentOffsetY(),
                            w - GetContentOffsetX() - ViewportW,
                            h - GetContentOffsetY() - ViewportH);
    page->OnViewportChanged(w, h);
  }
  Relayout();
}
void UPlayerWindowScreen::OnMetricsChanged(const GuiMetrics &metrics)
{
  UGuiScreenBase::OnMetricsChanged(metrics);
  Character->OnMetricsChanged(metrics);
  Catalog->OnMetricsChanged(metrics);
  Relayout();
}
void UPlayerWindowScreen::SelectSection(int section)
{
  const bool allowed = Session->GetWorldGameMode() == WorldGameMode::Creative;
  const int next = section == 4 && !allowed ? 0 : std::clamp(section, 0, 4);
  auto *selectedPage = next == 4 ? static_cast<UGuiScreenBase *>(Catalog) : Character;
  auto *selectedRoot = selectedPage->GetRoot();
  if (!Host || !selectedRoot)
    return;
  if (next != 4 && Character->GetSection() != next)
    Character->SetSection(next);
  if (Host->Page != selectedPage || !selectedRoot->IsVisible())
  {
    if (next == 4)
    {
      Catalog->SetVisible(true);
      Character->SetVisible(false);
    }
    else
    {
      Character->SetVisible(true);
      Catalog->SetVisible(false);
    }
  }
  Host->Page = selectedPage;
  Section = next;
  Tabs->SetTabEnabled(1, allowed);
  Tabs->SetActiveTab(TabForSection(Section));
}
void UPlayerWindowScreen::Update(double)
{
  Relayout();
  Window->SetTitle(Session->GetControlledDisplayName(),
                   Session->GetWorldGameMode() == WorldGameMode::Creative
                       ? " | Creative (vitals frozen)" : " | Survival");
  const auto views = Session->GetBarSlots(Session->GetActiveHotbarRow());
  for (size_t i = 0; i < Slots.size(); ++i)
  {
    auto *slot = Slots[i];
    const auto &view = views[i];
    slot->SetLabel(view.label);
    slot->SetCountHint(view.CountLabel);
    slot->SetSelected(view.selected);
    slot->SetWearProgress(view.wear);
    slot->SetBroken(view.broken);
    const unsigned texture =
        Icons ? (view.Entry.empty ? Icons->GetEntryIcon(view.entryKind, view.Id)
                                  : Icons->GetEntryIcon(view.Entry))
              : 0;
    slot->SetIconTexture(texture);
  }
}
bool UPlayerWindowScreen::PickHotbarSlot(int x, int y, SlotAddress &out) const
{
  for (size_t i = 0; i < Slots.size(); ++i)
    if (Slots[i]->GetBounds().Contains(x, y))
    {
      out = {};
      out.surface = SlotSurface::Hotbar;
      out.bar = Session->GetActiveHotbarRow();
      out.slot = i;
      return true;
    }
  return false;
}
GuiRect UPlayerWindowScreen::GetTabBounds(int section) const
{
  const auto pos = TabForSection(section);
  const auto b = Layout.Tabs;
  const int left = b.X + int(pos) * b.W / 5;
  return {left, b.Y, (int(pos) + 1) * b.W / 5 - int(pos) * b.W / 5, b.H};
}
GuiRect UPlayerWindowScreen::GetHotbarBounds(size_t index) const
{
  return index < Slots.size() ? Slots[index]->GetBounds() : GuiRect{};
}
unsigned UPlayerWindowScreen::GetHotbarIconTexture(size_t index) const
{
  return index < Slots.size() ? Slots[index]->GetIconTexture() : 0;
}
} // namespace cutum
