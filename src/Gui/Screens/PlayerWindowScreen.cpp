#include "Gui/Screens/PlayerWindowScreen.h"
#include "Game/GameSession.h"
#include "Gui/Core/GuiContext.h"
#include "Gui/Interfaces/IUGuiIconSource.h"
#include "Gui/Screens/CharacterSheetScreen.h"
#include "Gui/Screens/CreativePaletteScreen.h"
#include "Gui/Widgets/GuiSlot.h"
#include "Gui/Widgets/GuiTabBar.h"
#include "Gui/Widgets/GuiWindow.h"
#include <algorithm>
namespace cutum
{
// The pages keep their widget ownership and state. This host mounts the active
// page in one chrome/input tree without rebuilding or transferring its widgets.
class UPlayerSectionHost : public UGuiWidget
{
public:
  UGuiWidget *Page{nullptr};
  void Draw(UGuiRenderer &r) override
  {
    if (Page)
      Page->Draw(r);
  }
  UGuiWidget *HitTest(int x, int y) override
  {
    return Page && Bounds.Contains(x, y) ? Page->HitTest(x, y) : nullptr;
  }
  UGuiWidget *HitTestFocusable(int x, int y) override
  {
    return Page && Bounds.Contains(x, y) ? Page->HitTestFocusable(x, y)
                                         : nullptr;
  }
  bool ScrollAtPoint(int x, int y, const GuiScrollEvent &e) override
  {
    return Page && Bounds.Contains(x, y) && Page->ScrollAtPoint(x, y, e);
  }
  void CollectFocusables(std::vector<UGuiWidget *> &out) override
  {
    if (Page)
      Page->CollectFocusables(out);
  }
  bool OnMouseDown(const GuiMouseEvent &event) override
  {
    return Page && Bounds.Contains(event.X, event.Y) &&
           Page->OnMouseDown(event);
  }
  bool OnMouseUp(const GuiMouseEvent &event) override
  {
    return Page && Page->OnMouseUp(event);
  }
  bool OnMouseMove(const GuiMouseEvent &event) override
  {
    return Page && Page->OnMouseMove(event);
  }
  bool OnKey(const GuiKeyEvent &event) override
  {
    return Page && Page->OnKey(event);
  }
  bool OnChar(const GuiCharEvent &event) override
  {
    return Page && Page->OnChar(event);
  }
};
namespace
{
constexpr std::array<int, 5> Sections{0, 4, 1, 2, 3};
}
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
  auto window = std::make_unique<UGuiWindow>(&ctx.GetTheme(), "Player");
  Window = window.get();
  auto tabs = std::make_unique<UGuiTabBar>(&ctx.GetTheme());
  Tabs = tabs.get();
  Tabs->SetTabs({"Backpack", "Catalog", "Equipment", "Status", "Appearance"});
  Tabs->SetOnTabChanged([this](int tab)
                        { Session->OpenPlayerWindow(Sections[tab]); });
  window->AddChild(std::move(tabs));
  auto host = std::make_unique<UPlayerSectionHost>();
  Host = host.get();
  window->AddChild(std::move(host));
  for (size_t i = 0; i < Slots.size(); ++i)
  {
    auto slot = std::make_unique<UGuiSlot>(&ctx.GetTheme());
    Slots[i] = slot.get();
    slot->SetCornerHint(std::to_string((i + 1) % 10));
    slot->SetOnClick(
        [this, i]
        {
          const auto row = Session->GetCharacterStatsSnapshot().activeRow;
          if (!Session->ApplyPendingAssignment(row, i))
            Session->SelectSlot(row, i);
        });
    slot->SetOnBeginDrag(
        [this, i]
        {
          const auto row = Session->GetCharacterStatsSnapshot().activeRow;
          SlotAddress address;
          address.surface = SlotSurface::Hotbar;
          address.bar = row;
          address.slot = i;
          auto entry = Session->GetHotbarEntryRef(row, i);
          if (!entry.empty)
            Session->BeginDragFromSlot(address, entry);
        });
    window->AddChild(std::move(slot));
  }
  root->AddChild(std::move(window));
  Root = std::move(root);
  Character->SetHosted(true);
  Catalog->SetHosted(true);
  Relayout();
  SelectSection(0);
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
  Section = section == 4 && !allowed ? 0 : std::clamp(section, 0, 4);
  auto *selectedRoot = Section == 4 ? Catalog->GetRoot() : Character->GetRoot();
  if (!Host || Host->Page != selectedRoot || !selectedRoot->IsVisible())
  {
    Character->SetVisible(Section != 4);
    Catalog->SetVisible(Section == 4);
  }
  if (Section != 4 && Character->GetSection() != Section)
    Character->SetSection(Section);
  if (Host)
    Host->Page = Section == 4 ? Catalog->GetRoot() : Character->GetRoot();
  if (Tabs)
  {
    Tabs->SetTabEnabled({true, allowed, true, true, true});
    Tabs->SetActiveTab(
        int(std::find(Sections.begin(), Sections.end(), Section) -
            Sections.begin()));
  }
}
void UPlayerWindowScreen::Update(double)
{
  Relayout();
  const auto snap = Session->GetCharacterStatsSnapshot();
  Window->SetTitle(snap.displayName + (snap.gameMode == WorldGameMode::Creative
                                           ? " | Creative (vitals frozen)"
                                           : " | Survival"));
  const auto views = Session->GetBarSlots(snap.activeRow);
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
        Icons ? Icons->GetEntryIcon(view.entryKind, view.Id) : 0;
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
      out.bar = Session->GetCharacterStatsSnapshot().activeRow;
      out.slot = i;
      return true;
    }
  return false;
}
GuiRect UPlayerWindowScreen::GetTabBounds(int section) const
{
  const auto pos =
      std::find(Sections.begin(), Sections.end(), section) - Sections.begin();
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
