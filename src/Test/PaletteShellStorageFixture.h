#pragma once

#include "Gui/Preview/ContentPreviewDock.h"
#include "Gui/Preview/ContentPreviewRenderer.h"
#include "Gui/Screens/CharacterSheetScreen.h"
#include "Gui/Widgets/GuiLabel.h"
#include "Gui/Widgets/GuiPanel.h"
#include "Gui/Widgets/GuiPreviewViewport.h"
#include "Gui/Widgets/GuiScrollView.h"
#include "Gui/Widgets/GuiTabBar.h"
#include "Test/PaletteGridStorageFixture.h"
#include "Test/IconLifecycleFixture.h"

namespace cutum::palette_shell_fixture
{
using gui_context_fixture::Require;
using Probe = PaletteGridSourceProbe;

class Catalog final : public IUContentCatalog
{
public:
  explicit Catalog(const UContentTypeRegistry &registry) : Registry(registry) {}
  const UContentTypeRegistry &Registry;
  std::array<std::string, 2> Types{std::string(42, 'a'), std::string(44, 'b')};

  std::vector<std::string> GetTypeIds(ContentKind) const override
  {
    return {Types[0], Types[1]};
  }
  std::string GetTypeDisplayName(const std::string &id) const override
  {
    return "Catalog group " + id;
  }
  std::vector<CatalogEntry> GetEntries(ContentKind kind, const std::string &type) const override
  {
    return Registry.GetEntries(kind, type);
  }
};

inline size_t CheckPreviewSelection(const GuiTheme &theme)
{
  const std::string oldId(60, 'a'), newId(80, 'b');
  const std::string oldTitle(40, 'c'), newTitle(100, 'd');
  size_t failures{};
  for (size_t cutoff = 0; ; ++cutoff)
  {
    UContentPreviewDock dock(&theme, nullptr);
    auto &title = static_cast<UGuiLabel &>(*dock.GetPanel()->GetChildren().front());
    dock.SetSelection(ContentKind::Block, oldId, oldTitle);
    const auto previous = dock.GetViewStateKey();
    observation::Scope epoch(cutoff);
    bool refused = false;
    try { dock.SetSelection(ContentKind::Object, newId, newTitle); }
    catch (const std::bad_alloc &) { refused = true; }
    observation::State.fail_after = SIZE_MAX;
    if (refused)
    {
      ++failures;
      const bool sameSelection = dock.GetViewStateKey() == previous;
      Require(title.GetText() == oldTitle && sameSelection && !observation::State.live,
              "preview selection refusal preserves previous identity and title");
      dock.SetSelection(ContentKind::Object, newId, newTitle);
    }
    Require(title.GetText() == newTitle && dock.GetViewStateKey().compare(0, newId.size(), newId) == 0,
            "preview selection publishes title and identity together");
    dock.ClearSelection();
    Require(title.GetText() == "Preview" && dock.GetViewStateKey().front() == '|',
            "clearing a preview retires its selection");
    if (!refused)
      break;
  }
  Require(!observation::State.live && !observation::State.blocks,
          "all preview selection requests retire");
  return failures;
}

inline void CheckPreviewGlBindings()
{
  using icon_lifecycle_fixture::MakeTexture;
  UContentPreviewRenderer renderer({}, {}, {}, {});
  auto unit0 = MakeTexture();
  auto unit2 = MakeTexture();
  UGlFramebufferOwner draw, read;
  UGlStateScope restore(kGlMaskIconFbo | GlStateBit::DepthMask);
  glBindFramebuffer(GL_DRAW_FRAMEBUFFER, draw.Get());
  glBindFramebuffer(GL_READ_FRAMEBUFFER, read.Get());
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, unit0.Get());
  glActiveTexture(GL_TEXTURE2);
  glBindTexture(GL_TEXTURE_2D, unit2.Get());
  glDepthMask(GL_FALSE);
  for (const auto kind : {ContentKind::Block, ContentKind::Object})
  {
    for (const int size : {128, 128, 256})
    {
      Require(renderer.RenderUnique(kind, "missing_fixture_definition", size, 45.f, 32.f) == 0,
              "missing preview source retires unpublished texture");
      GLint active{}, texture{}, readName{}, drawName{};
      GLboolean depthMask{};
      glGetIntegerv(GL_ACTIVE_TEXTURE, &active);
      glGetIntegerv(GL_TEXTURE_BINDING_2D, &texture);
      glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &readName);
      glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &drawName);
      glGetBooleanv(GL_DEPTH_WRITEMASK, &depthMask);
      Require(active == GL_TEXTURE2 && GLuint(texture) == unit2.Get() &&
                  GLuint(readName) == read.Get() && GLuint(drawName) == draw.Get() &&
                  depthMask == GL_FALSE && glGetError() == GL_NO_ERROR,
              "unique preview failure restores caller framebuffer and render state");
      glActiveTexture(GL_TEXTURE0);
      glGetIntegerv(GL_TEXTURE_BINDING_2D, &texture);
      glActiveTexture(GL_TEXTURE2);
      Require(GLuint(texture) == unit0.Get(), "unique preview keeps caller texture unit zero");
    }
  }
}

inline nlohmann::json Run()
{
  UGuiContext context;
  Require(context.Initialize(std::make_shared<UShaderManager>(), {}),
          "palette shell fixture initializes renderer");
  UGameSession session(nullptr, nullptr);
  auto &registry = session.GetContentCatalog();
  UBlockDefinitionStorage blocks;
  std::unordered_map<BlockId, BlockDefinition> definitions;
  std::unordered_map<std::string, BlockId> names;
  for (BlockId id = 1; id <= 160; ++id)
  {
    BlockDefinition block;
    block.Id = id;
    block.Name = "fixture_catalog_block_" + std::to_string(id);
    block.DisplayName = "Fixture block " + std::to_string(id);
    names.emplace(block.Name, id);
    definitions.emplace(id, std::move(block));
  }
  blocks.ReplaceAll(std::move(definitions), std::move(names));
  registry.IndexBlocks(blocks);
  Catalog catalog(registry);
  for (const auto kind : {ContentKind::Block, ContentKind::Object, ContentKind::UCreature,
                           ContentKind::Skin, ContentKind::Item})
    for (const auto mode : {InventoryMode::Owned, InventoryMode::Creative})
    {
      const bool expected = !session.GetEntries(kind, catalog.Types[0], mode).empty();
      observation::Scope epoch(0);
      Require(session.HasEntries(kind, catalog.Types[0], mode) == expected &&
                  !observation::State.attempts,
              "availability matches materialized routes and allocates nothing");
    }

  const auto make = [&]
  {
    auto palette = std::make_unique<UCreativePaletteScreen>(&catalog, &session, nullptr, nullptr);
    palette->OnAttach(context);
    return palette;
  };
  size_t initial{}, initialBytes{};
  for (size_t cutoff = 0; ; ++cutoff)
  {
    auto page = make();
    observation::Scope epoch(cutoff);
    bool refused = false;
    try { page->Build(context); }
    catch (const std::bad_alloc &) { refused = true; }
    observation::State.fail_after = SIZE_MAX;
    if (refused)
    {
      Require(!page->GetRoot() && !page->GetGridEntryCount() && !observation::State.live,
              "cold palette refusal leaves no published widget or memory");
      page->Build(context);
    }
    initialBytes = observation::State.live;
    Require(!page->GetRoot()->IsVisible(), "cold complete page respects visibility");
    page.reset();
    Require(!observation::State.live && !observation::State.blocks, "cold palette requests retire");
    if (!refused)
    {
      initial = cutoff;
      break;
    }
  }

  auto built = [&]
  {
    auto page = make();
    page->Build(context);
    page->SetVisible(true);
    page->OnViewportChanged(1000, 560);
    page->Update(0);
    return page;
  };
  size_t replacement{}, replacementBytes{};
  for (size_t cutoff = 0; ; ++cutoff)
  {
    auto page = built();
    const auto &last = Probe::StripSlot(*page, 9).GetBounds();
    SlotAddress address;
    Require(page->PickHotbarStrip(last.X + 1, last.Y + 1, address) && address.slot == 9,
            "standalone quick slot is routed at its actual bounds");
    UCharacterSheetScreen character(nullptr, nullptr);
    character.OnAttach(context);
    character.Build(context);
    UPlayerWindowScreen window(&session, &character, page.get(), nullptr);
    window.OnAttach(context);
    window.Build(context);
    window.OnViewportChanged(1000, 560);
    window.SelectSection(4);

    auto &tabs = Probe::Types(*page);
    auto &scroll = Probe::Scroll(*page);
    auto &dock = Probe::Dock(*page);
    auto *const oldRoot = page->GetRoot();
    auto *const first = &Probe::First(*page);
    auto &host = *window.GetRoot()->GetChildren()[0]->GetChildren()[1];
    scroll.SetScrollY(48);
    dock.SetSelection(ContentKind::Block, "fixture_selection", "Fixture preview");
    const auto state = page->GetSectionStateKey();
    const auto bounds = tabs.GetBounds();
    const int x = bounds.X + 1, y = bounds.Y + 1;
    Require(page->GetGridEntryCount() == 160 && scroll.GetScrollY() > 0 &&
                host.HitTest(x, y) == &tabs && !Probe::StripSlot(*page, 9).IsVisible(),
            "host routes live tabs and grid while its own strip covers hotbar input");

    observation::Scope epoch(cutoff);
    bool refused = false;
    try { page->Build(context); }
    catch (const std::bad_alloc &) { refused = true; }
    observation::State.fail_after = SIZE_MAX;
    if (refused)
    {
      Require(page->GetRoot() == oldRoot && host.HitTest(x, y) == &tabs &&
                  !observation::State.live && page->GetSectionStateKey() == state,
              "refused palette shell leaves the old tree and selection published");
      page->Build(context);
    }
    replacementBytes = observation::State.live;
    Require(page->GetRoot() != oldRoot && page->GetRoot()->IsVisible() &&
                host.HitTest(x, y) == &tabs && &Probe::First(*page) == first &&
                &Probe::Types(*page) == &tabs && &Probe::Scroll(*page) == &scroll &&
                &Probe::Dock(*page) == &dock && page->GetSectionStateKey() == state,
            "replacement retains live grid, tabs, scroll, preview and host routing");
    page.reset();
    Require(!observation::State.live && !observation::State.blocks, "palette shell replacement retires");
    if (!refused)
    {
      replacement = cutoff;
      break;
    }
  }

  const auto originalTypes = catalog.Types;
  const auto checkTabs = [&](bool empty)
  {
    for (size_t cutoff = 0; ; ++cutoff)
    {
      catalog.Types = originalTypes;
      auto page = built();
      page->OpenWithMainTab(1);
      if (!empty)
      {
        page->Update(0);
        page->OpenWithMainTab(0);
      }
      auto &tabs = Probe::Types(*page);
      const auto previousLabels = tabs.GetTabLabels();
      const auto previousFlags = tabs.GetEnabledTabs();
      const auto previousType = Probe::ActiveType(*page);
      catalog.Types[0].assign(66, 'x');
      catalog.Types[1].assign(70, 'y');
      observation::Scope epoch(cutoff);
      bool refused = false;
      try { Probe::SyncTypes(*page); }
      catch (const std::bad_alloc &) { refused = true; }
      observation::State.fail_after = SIZE_MAX;
      if (refused)
      {
        Require(tabs.GetTabLabels() == previousLabels && tabs.GetEnabledTabs() == previousFlags &&
                    Probe::ActiveType(*page) == previousType && !observation::State.live,
                "type refresh failure preserves published labels, availability and selection");
        Probe::SyncTypes(*page);
      }
      const auto &active = Probe::ActiveType(*page);
      Require(tabs.GetTabLabels().front() == catalog.GetTypeDisplayName(catalog.Types[0]) &&
                  tabs.GetEnabledTabs()[0] == !empty && tabs.GetEnabledTabs()[1] == !empty &&
                  (empty ? active.empty() : active == catalog.Types[0]),
              "type refresh publishes rows and the matching selection");
      if (!empty)
      {
        const auto bounds = tabs.GetBounds();
        const GuiMouseEvent press{bounds.X + bounds.W / 2 + 1, bounds.Y + 1};
        observation::State.fail_after = observation::State.attempts;
        Require(tabs.OnMouseDown(press) && tabs.GetActiveTab() == 0 && active == catalog.Types[0],
                "refused type input returns to the published selection");
        observation::State.fail_after = SIZE_MAX;
        Require(tabs.OnMouseDown(press) && tabs.GetActiveTab() == 1 && active == catalog.Types[1],
                "type input selects the requested row after retry");
        page->OpenWithMainTab(0);
        Probe::SyncTypes(*page);
        Require(tabs.GetActiveTab() == 0 && active == catalog.Types[0],
                "reopening the same category resets the selection to its default row");
      }
      page.reset();
      Require(!observation::State.live && !observation::State.blocks, "tab storage and scratch retire");
      if (!refused)
        return cutoff;
    }
  };
  const auto emptyTabFailures = checkTabs(true);
  const auto tabFailures = checkTabs(false);

  const auto selectionFailures = CheckPreviewSelection(context.GetTheme());
  CheckPreviewGlBindings();
  Require(initial > replacement && replacement && tabFailures > emptyTabFailures && selectionFailures,
          "cold, replacement, type and preview paths all exercise refusal");
  return {{"status", "PASS"}, {"initial_refusal_prefixes", initial},
          {"initial_request_bytes", initialBytes}, {"replacement_refusal_prefixes", replacement},
          {"replacement_request_bytes", replacementBytes}, {"type_refusal_prefixes", tabFailures},
          {"empty_type_refusal_prefixes", emptyTabFailures}, {"type_input_retry", true},
          {"preview_selection_refusals", selectionFailures}, {"availability_allocate0", true},
          {"live_state_retained", true}, {"preview_gl_state_restored", true}};
}
}
