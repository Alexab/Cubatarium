#pragma once

#include "Game/GameSession.h"
#include "Gui/Interfaces/IUCharacterStatsViewModel.h"
#include "Gui/Screens/CharacterSheetScreen.h"
#include "Gui/Widgets/GuiLabel.h"
#include "Gui/Widgets/GuiPreviewViewport.h"
#include "Gui/Widgets/GuiScrollView.h"
#include "Gui/Widgets/GuiTextInput.h"
#include "Test/GuiContextStorageFixture.h"
#include "Test/OrdinaryNewObservation.h"

namespace cutum::character_page_fixture
{
using gui_context_fixture::Require;

class Stats final : public IUCharacterStatsViewModel
{
public:
  CharacterStatsSnapshot Source;
  mutable size_t Reads{};

  CharacterStatsSnapshot GetCharacterStatsSnapshot() const override
  {
    ++Reads;
    return Source;
  }
  WorldGameMode GetWorldGameMode() const override { return Source.gameMode; }
  WorldDifficulty GetWorldDifficulty() const override { return Source.difficulty; }
};

template <typename Widget>
Widget *Find(UGuiWidget &root)
{
  if (auto *match = dynamic_cast<Widget *>(&root))
    return match;
  for (const auto &child : root.GetChildren())
    if (auto *match = Find<Widget>(*child))
      return match;
  return nullptr;
}

inline nlohmann::json Run()
{
  UGuiContext context;
  Require(context.Initialize(std::make_shared<UShaderManager>(), {}),
          "character page fixture renderer initialized");
  Stats stats;
  stats.Source.valid = true;
  stats.Source.displayName = "Character page storage fixture";
  stats.Source.OwnerId = 17;
  stats.Source.typeId = "human";
  const auto make = [&]
  {
    auto page = std::make_unique<UCharacterSheetScreen>(&stats, nullptr);
    page->OnAttach(context);
    return page;
  };
  const auto build = [&]
  {
    auto page = make();
    page->Build(context);
    return page;
  };

  size_t coldAttempts{}, coldBytes{};
  for (size_t cutoff = 0; ; ++cutoff)
  {
    auto page = make();
    observation::Scope epoch(cutoff);
    bool refused = false;
    try { page->Build(context); }
    catch (const std::bad_alloc &) { refused = true; }
    Require(refused ? (!page->GetRoot() && !observation::State.live)
                    : (page->GetRoot() && !page->GetRoot()->IsVisible()),
            "cold page publishes only on complete construction");
    observation::State.fail_after = SIZE_MAX;
    if (refused)
      page->Build(context);
    Require(page->GetBackpackSlotBounds(39).W > 0 && stats.Reads == 0,
            "cold page is laid out without taking a character snapshot");
    coldBytes = observation::State.live;
    page.reset();
    Require(!observation::State.live && !observation::State.blocks,
            "cold page requests retire together");
    if (!refused)
    {
      coldAttempts = cutoff;
      break;
    }
  }

  const StatusEffectInstance first{std::string(47, 'a'), 8.25f, 0.f, 2};
  const StatusEffectInstance second{std::string(49, 'b'), 2.f, 0.f, 1};
  stats.Source.effects.assign(29, first);
  UGameSession session(nullptr, nullptr);
  UCreativePaletteScreen catalog(&session.GetContentCatalog(), &session, nullptr, nullptr);
  catalog.OnAttach(context);
  catalog.Build(context);

  const auto characterWindow = [&]
  {
    auto page = build();
    auto window = std::make_unique<UPlayerWindowScreen>(&session, page.get(), &catalog, nullptr);
    window->OnAttach(context);
    window->Build(context);
    window->OnViewportChanged(1280, 720);
    return std::pair(std::move(page), std::move(window));
  };
  const auto activeHost = [](UPlayerWindowScreen &window) -> UGuiWidget &
  {
    return *window.GetRoot()->GetChildren()[0]->GetChildren()[1];
  };

  size_t replacementAttempts{}, replacementBytes{};
  for (size_t cutoff = 0; ; ++cutoff)
  {
    auto [page, window] = characterWindow();
    auto *const oldRoot = page->GetRoot();
    auto *const search = Find<UGuiTextInput>(*oldRoot);
    auto *const preview = Find<UGuiPreviewViewport>(*oldRoot);
    auto *const scroll = Find<UGuiScrollView>(*oldRoot);
    Require(search && preview && scroll, "the active character page has all interactive bindings");
    search->SetText(std::string(60, 's'));
    preview->SetAngles(15, 26);
    scroll->SetScrollY(180);
    Require(scroll->GetScrollY() > 0, "the status fixture is scrollable");
    const auto state = page->GetSectionStateKey();
    auto &host = activeHost(*window);
    const auto bounds = search->GetBounds();
    const int x = bounds.X + bounds.W / 2, y = bounds.Y + bounds.H / 2;
    Require(host.HitTestFocusable(x, y) == search, "host routes to the active search field");

    observation::Scope epoch(cutoff);
    bool refused = false;
    try { page->Build(context); }
    catch (const std::bad_alloc &) { refused = true; }
    observation::State.fail_after = SIZE_MAX;
    if (refused)
    {
      Require(page->GetRoot() == oldRoot && host.HitTestFocusable(x, y) == search &&
                  !observation::State.live && page->GetSectionStateKey() == state,
              "refused replacement preserves the old tree, input and page state");
      page->Build(context);
    }
    replacementBytes = observation::State.live;
    Require(page->GetRoot() != oldRoot && page->GetRoot()->IsVisible() &&
                host.HitTestFocusable(x, y) == Find<UGuiTextInput>(*page->GetRoot()) &&
                page->GetSectionStateKey() == state,
            "the active host resolves the replacement and preserves search, rotation and scroll");
    window.reset();
    page.reset();
    Require(!observation::State.live && !observation::State.blocks,
            "replacement page and staged backing retire");
    if (!refused)
    {
      replacementAttempts = cutoff;
      break;
    }
  }

  const auto statusChanges = [&](bool shrink)
  {
    constexpr size_t baseRows = 17;
    const size_t previousCount = shrink ? 4 : 1;
    const size_t nextCount = shrink ? 1 : 4;
    size_t beforePublication{};
    for (size_t cutoff = 0; ; ++cutoff)
    {
      stats.Source.effects.assign(previousCount, first);
      auto page = build();
      page->SetVisible(true);
      auto &scroll = *Find<UGuiScrollView>(*page->GetRoot());
      const auto &rows = scroll.Content().GetChildren();
      Require(rows.size() == baseRows + previousCount, "initial status tail has the expected length");
      const auto *const movement = rows[baseRows - 1].get();
      const auto *const oldFirst = rows[baseRows].get();
      const auto priorCapacity = rows.capacity();
      const auto beforeRead = stats.Reads;
      stats.Source.effects.assign(nextCount, second);
      const std::string expected = second.DefId + " x1  2s";
      observation::Scope epoch(cutoff);
      page->Update(0);
      const bool refused = observation::State.refusals > 0;
      observation::State.fail_after = SIZE_MAX;
      Require(stats.Reads == beforeRead + 1 && rows[baseRows - 1].get() == movement,
              "refresh reads one snapshot and keeps the unchanged movement row");
      if (rows.size() == baseRows + previousCount)
      {
        ++beforePublication;
        Require(refused && rows[baseRows].get() == oldFirst && rows.capacity() == priorCapacity,
                "refused status refresh retains prior labels and child backing");
        page->Update(0);
      }
      Require(rows.size() == baseRows + nextCount &&
                  (!shrink || rows.capacity() < priorCapacity),
              "complete status publication retires the removed rows and backing");
      for (size_t i = baseRows; i < rows.size(); ++i)
        Require(static_cast<const UGuiLabel &>(*rows[i]).GetText() == expected,
                "no partially published status text after refresh");
      const auto used = observation::State.attempts;
      observation::State.fail_after = used;
      page->OnViewportChanged(1152, 720);
      scroll.SetScrollY(40);
      Require(observation::State.attempts == used,
              "status resize and scrolling do not allocate scratch child pointers");
      observation::State.fail_after = SIZE_MAX;
      page.reset();
      Require(!observation::State.live && !observation::State.blocks,
              "status storage and refresh scratch retire");
      if (!refused)
        break;
    }
    return beforePublication;
  };
  const size_t growthRefusals = statusChanges(false);
  const size_t shrinkRefusals = statusChanges(true);
  Require(coldAttempts && replacementAttempts > coldAttempts && growthRefusals && shrinkRefusals,
          "the fixture exercises refusal before all four publications");
  return {{"status", "PASS"}, {"initial_refusal_prefixes", coldAttempts},
          {"initial_tree_requests", coldBytes}, {"replacement_refusal_prefixes", replacementAttempts},
          {"replacement_tree_requests", replacementBytes}, {"status_growth_refusals", growthRefusals},
          {"status_shrink_refusals", shrinkRefusals}, {"snapshots_per_refresh", 1},
          {"host_reroutes", true}, {"retained_state", true}, {"scroll_layout_allocate0", true}};
}
}
