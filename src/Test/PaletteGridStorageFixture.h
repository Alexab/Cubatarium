#pragma once

#include "Blocks/BlockDefinitionStorage.h"
#include "Game/GameSession.h"
#include "Gui/Cache/IconWarmupQueue.h"
#include "Gui/Interfaces/IUGuiIconSource.h"
#include "Gui/Widgets/GuiSlot.h"
#include "Test/GuiContextStorageFixture.h"
#include "Test/OrdinaryNewObservation.h"
#include <charconv>

namespace cutum
{
struct PaletteGridSourceProbe
{
  static void Rebuild(UCreativePaletteScreen &palette) { palette.RebuildGrid(); }
  static void Append(UCreativePaletteScreen &palette) { palette.AppendGridSlotsChunk(); }
  static UGuiSlot &First(UCreativePaletteScreen &palette)
  {
    return static_cast<UGuiSlot &>(*palette.GridChildren().front());
  }
};

class PaletteGridFixtureIcons final : public IUGuiIconSource
{
public:
  static constexpr size_t Count = 620;
  mutable size_t CachedChecks{};
  unsigned GetBlockIconTexture(const std::string &id) override
  {
    Ready[Index(id)] = true;
    return 1;
  }
  unsigned GetBlockIconTextureIfCached(const std::string &id) const override
  {
    ++CachedChecks;
    return Ready[Index(id)] ? 1 : 0;
  }
  void QueueBlockIcon(const std::string &id) override { Queue.Push(id); }
  void WarmupBlockIcons(size_t count) override
  {
    for (size_t i = 0; i < count; ++i)
      if (!Queue.RunFront([this](const auto &id) { GetBlockIconTexture(id); }))
        break;
  }
  size_t PendingBlockIcons() const override { return Queue.Size(); }
  unsigned GetObjectIconTexture(const std::string &) override { return 0; }
  unsigned GetObjectIconTextureIfCached(const std::string &) const override { return 0; }
  unsigned GetCreatureIconTexture(const std::string &) override { return 0; }
  unsigned GetSkinIconTexture(const std::string &) override { return 0; }
  unsigned GetItemIconTexture(const std::string &) override { return 0; }
  void WarmupItemIcons(size_t) override {}
  size_t PendingItemIcons() const override { return 0; }

private:
  static size_t Index(const std::string &id)
  {
    size_t number{};
    const auto result = std::from_chars(id.data() + id.size() - 4, id.data() + id.size(), number);
    gui_context_fixture::Require(result.ec == std::errc{} && number >= 1000 && number < 1000 + Count,
                                 "unexpected block ID in icon fixture");
    return number - 1000;
  }
  std::array<bool, Count> Ready{};
  UGuiIconWarmupQueue Queue;
};

inline size_t CheckIconQueueRefusals()
{
  using gui_context_fixture::Require;
  const std::string first(70, 'a'), second(90, 'b');
  size_t requests{};
  {
    observation::Scope epoch;
    UGuiIconWarmupQueue queue;
    const auto before = observation::State.attempts;
    Require(queue.Push(first), "icon queue first insertion");
    requests = observation::State.attempts - before;
  }
  for (size_t prefix = 0; prefix < requests; ++prefix)
  {
    observation::Scope epoch;
    {
      UGuiIconWarmupQueue queue;
      observation::State.fail_after = observation::State.attempts + prefix;
      bool refused = false;
      try { queue.Push(first); }
      catch (const std::bad_alloc &) { refused = true; }
      observation::State.fail_after = SIZE_MAX;
      Require(refused && queue.Empty() && queue.Push(first) && !queue.Push(first) && queue.Push(second),
              "icon queue refusal preserves retry, deduplication and pending work");
      Require(queue.RunFront([&](const auto &id) { Require(id == first, "first queued icon"); }) &&
                  queue.RunFront([&](const auto &id) { Require(id == second, "second queued icon"); }) && queue.Empty(),
              "icon queue retains FIFO ordering through refusal and retry");
    }
    Require(!observation::State.live && !observation::State.blocks,
            "icon queue backing retires on destruction");
  }
  return requests;
}

inline nlohmann::json CheckPaletteGridRequests()
{
  using gui_context_fixture::Require;
  using Probe = PaletteGridSourceProbe;
  UGuiContext context;
  Require(context.Initialize(std::make_shared<UShaderManager>(), {}), "palette fixture initializes renderer");
  UGameSession session(nullptr, nullptr);
  auto &catalog = session.GetContentCatalog();
  UBlockDefinitionStorage blocks;
  std::unordered_map<BlockId, BlockDefinition> definitions;
  std::unordered_map<std::string, BlockId> names;
  for (size_t i = 0; i < PaletteGridFixtureIcons::Count; ++i)
  {
    const auto key = std::to_string(1000 + i);
    const auto id = static_cast<BlockId>(i + 1);
    BlockDefinition block;
    block.Id = id;
    block.Name = "fixture_block_with_long_name_" + key;
    block.DisplayName = "Fixture catalog label " + key;
    names.emplace(block.Name, id);
    definitions.emplace(id, std::move(block));
  }
  blocks.ReplaceAll(std::move(definitions), std::move(names));
  catalog.IndexBlocks(blocks);
  const auto make = [&](IUGuiIconSource *icons) {
    auto screen = std::make_unique<UCreativePaletteScreen>(&catalog, &session, icons, nullptr);
    screen->OnAttach(context);
    screen->Build(context);
    screen->SetHosted(true);
    screen->OnViewportChanged(1280, 720);
    screen->OpenWithMainTab(0);
    return screen;
  };
  auto palette = make(nullptr);
  PaletteGridStorage first, second, complete;
  size_t rebuildRequests{}, appendRequests{}, replacementRefusals{}, appendRefusals{}, peak{};
  {
    observation::Scope epoch;
    auto observe = [&] {
      const auto snapshot = palette->InspectGridStorage(catalog, session);
      Require(snapshot.supported && snapshot.requested == observation::State.live &&
                  snapshot.callbacks == snapshot.widgets * 2 && !snapshot.callback_bytes,
              "published palette grid matches independently measured requested bytes");
      return snapshot;
    };
    Probe::Rebuild(*palette);
    rebuildRequests = observation::State.attempts;
    first = observe();
    Require(first.rows == 620 && first.widgets == 256 && !first.complete,
            "first palette publication contains at most one batch");
    const auto beforeAppend = observation::State.attempts;
    Probe::Append(*palette);
    appendRequests = observation::State.attempts - beforeAppend;
    second = observe();
    Require(second.widgets == 512 && second.row_text_bytes < first.row_text_bytes,
            "appended labels retire their staged copies");
    Probe::Append(*palette);
    complete = observe();
    Require(complete.widgets == complete.rows && complete.complete,
            "final catalogue batch completes the original rows");

    auto refuse = [&](size_t requests, bool replace) {
      const auto previous = observe();
      auto *widget = &Probe::First(*palette);
      for (const size_t prefix : {size_t{0}, size_t{1}, requests / 2, requests - 1})
      {
        observation::State.fail_after = observation::State.attempts + prefix;
        bool refused = false;
        try
        {
          if (replace) Probe::Rebuild(*palette);
          else Probe::Append(*palette);
        }
        catch (const std::bad_alloc &) { refused = true; }
        const auto during = observation::State.attempts;
        const auto observed = observe();
        Require(refused && observed.requested == previous.requested &&
                    observed.widgets == previous.widgets && &Probe::First(*palette) == widget &&
                    during == observation::State.attempts,
                "refused materialization preserves the full published prefix and its aliases");
        observation::State.fail_after = SIZE_MAX;
        ++(replace ? replacementRefusals : appendRefusals);
      }
    };
    refuse(rebuildRequests, true);
    Probe::Rebuild(*palette);
    Require(observe().widgets == 256, "same-input replacement succeeds after allocation refusal");
    refuse(appendRequests, false);
    Probe::Append(*palette);
    Require(observe().widgets == 512, "same-input append succeeds after allocation refusal");
    peak = observation::State.peak;
    Probe::First(*palette).SetOnClick([data = std::array<char, 512>{}] { (void)data; });
    Require(!palette->InspectGridStorage(catalog, session).supported, "opaque palette action is rejected");
    palette.reset();
    Require(!observation::State.live && !observation::State.blocks, "palette grid rows and widgets retire together");
  }

  PaletteGridFixtureIcons icons;
  palette = make(&icons);
  palette->Update(0);
  const auto firstIcons = palette->GetGridIconCount();
  palette->Update(0);
  Require(palette->GetGridIconCount() > firstIcons && icons.PendingBlockIcons() > 0,
          "visible textures publish while background icon requests remain queued");
  palette->Update(0);
  const auto beforeChecks = icons.CachedChecks;
  palette->Update(0);
  const auto cacheChecks = icons.CachedChecks - beforeChecks;
  Require(palette->IsGridComplete() && cacheChecks < 200,
          "published large grid checks a visible range and bounded background batch");

  auto &held = Probe::First(*palette);
  const auto bounds = held.GetBounds();
  GuiMouseEvent press{bounds.X + 2, bounds.Y + 2};
  press.Button = GuiMouseButton::Left;
  const bool pressed = held.OnMouseDown(press);
  palette->SetPointerPressed(true);
  palette->OpenWithMainTab(4);
  palette->Update(0);
  SlotAddress source;
  Require(pressed && &Probe::First(*palette) == &held &&
              palette->PickGridSlot(press.X, press.Y, source) && source.paletteKind == ContentKind::Block,
          "pending tab switch keeps the captured widget and published classification");
  auto move = press;
  move.X += 40;
  held.OnMouseMove(move);
  const auto &drag = session.GetDrag();
  Require(drag.Active && drag.source.paletteKind == ContentKind::Block && drag.entry.Id == source.entryId,
          "slot action dispatch uses the matching row from the published grid");
  held.OnMouseUp(move);
  session.CancelDrag();
  palette->SetPointerPressed(false);
  palette->Update(0);
  Require(palette->IsGridComplete() && palette->GetGridEntryCount() == 0,
          "released capture allows the requested empty item grid to publish");
  return {{"status", "PASS"}, {"first", PaletteGridStorageJson(first)},
          {"second", PaletteGridStorageJson(second)}, {"complete", PaletteGridStorageJson(complete)},
          {"rebuild_requests", rebuildRequests}, {"append_requests", appendRequests},
          {"rebuild_refusals", replacementRefusals}, {"append_refusals", appendRefusals},
          {"peak", peak}, {"steady_icon_cache_checks", cacheChecks},
          {"queue_refusals", CheckIconQueueRefusals()}, {"published_drag_identity", true}, {"retired", true}};
}
}
