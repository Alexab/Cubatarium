#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "Test/RootSourceGlContext.h"
#include "Test/OrdinaryNewObservation.h"
#include "Test/ApplicationOperationStorageFixture.h"
#include "Test/GuiContextStorageFixture.h"
#include "Test/MainMenuStorageFixture.h"
#include "Test/InGameHudStorageFixture.h"
#include "Test/ConsoleStorageFixture.h"
#include "Test/PlayerWindowStorageFixture.h"
#include "Test/CharacterPageStorageFixture.h"
#include "Test/PaletteGridStorageFixture.h"
#include "Test/PaletteShellStorageFixture.h"
#include "Test/IconLifecycleFixture.h"
#include "Test/CharacterSnapshotStorageFixture.h"
#include "Test/CraftingStorageFixture.h"
#include "Test/AnvilStorageFixture.h"
#include "Test/WorldGenPaletteStorageFixture.h"
#include "Test/SharedContentPreviewStorageFixture.h"
#include "Test/ObjectIconStorageFixture.h"
#include "Test/InventoryIconServiceStorageFixture.h"
#include "Test/CreatureItemIconStorageFixture.h"
#include "Test/SharedVisualSourceStorageFixture.h"
#include "Test/VisualLoaderSourceFixture.h"
#include "Test/SharedTextureSourceStorageFixture.h"
#include "Test/InventoryIconCodecFixture.h"
#include "Gui/Widgets/GuiButton.h"
#include "Gui/Widgets/GuiLabel.h"
#include <fstream>
#include <iostream>
namespace {
// Independent original capture declarations preserve the pre-change ABI and
// copy/move traits;these lambdas never run and contain no production logic.
void CheckOriginalCaptureTypes(cutum::UNewWorldScreen *owner,const cutum::WorldRunnerRequest &input){
  const cutum::ProceduralSettings settings=input.settings;auto packs=input.packs;const auto view=input.view;const auto gameMode=input.gameMode;const auto difficulty=input.difficulty;
  const auto create=[owner,settings,packs,view,gameMode,difficulty](){};
  const std::string worldName=input.worldName;cutum::ULoadWorldScreen *load_owner=nullptr;const auto load=[load_owner,worldName](){};
  static_assert(sizeof(create)==sizeof(cutum::CreateWorldMenuAction));static_assert(sizeof(load)==sizeof(cutum::LoadWorldMenuAction));
  static_assert(std::is_nothrow_move_constructible_v<decltype(create)> ==std::is_nothrow_move_constructible_v<cutum::CreateWorldMenuAction>);
  static_assert(std::is_nothrow_move_constructible_v<decltype(load)> ==std::is_nothrow_move_constructible_v<cutum::LoadWorldMenuAction>);
}

nlohmann::json CheckGuiContextRequests()
{
  using namespace cutum;
  using gui_context_fixture::Require;
  auto lifecycle = gui_context_fixture::RunLifecycle();
  size_t prefixes = 0;
  {
    UGuiContext cold;
    observation::Scope observed;
    Require(!cold.Initialize({}, {}), "renderer initialization rejects absent shader manager");
    prefixes = observation::State.attempts;
    Require(prefixes && !observation::State.live && !cold.InspectOwnedStorage().renderer_bytes,
            "failed renderer initialization retires ordinary allocations");
  }
  for (size_t n = 0; n < prefixes; ++n)
  {
    UGuiContext cold;
    observation::Scope observed(n);
    bool refused = false;
    try { cold.Initialize({}, {}); }
    catch (const std::bad_alloc &) { refused = true; }
    Require(refused && !observation::State.live && cold.InspectOwnedStorage().supported &&
                !cold.InspectOwnedStorage().router_bytes,
            "each GUI initialization allocation refusal leaves a cold context");
  }

  auto shaders = std::make_shared<UShaderManager>();
  auto context = std::make_unique<UGuiContext>();
  Require(context->Initialize(shaders, {}), "GUI ordinary observation initializes");
  gui_context_fixture::AttachProgressScreen(*context);
  auto &renderer = context->GetRenderer();
  const GuiRect rect{0, 0, 16, 16};
  const glm::vec4 color{1.f};
  renderer.BeginFrame(320, 240);
  renderer.DrawFilledRect(rect, color);
  renderer.EndFrame();

  GuiContextStorage grown, cleared, stopped;
  size_t notifications = 0, observed_requests = 0;
  const auto listener = [&](const GuiMetrics &metrics) {
    if (metrics.Scale == 1.25f)
      ++notifications;
  };
  {
    observation::Scope observed;
    for (size_t n = 0; n < 128; ++n)
      context->AddMetricsChangedListener(listener);
    auto full = context->InspectOwnedStorage();
    while (full.listeners < full.listener_capacity)
    {
      context->AddMetricsChangedListener(listener);
      full = context->InspectOwnedStorage();
    }
    Require(!full.supported && full.listeners >= 128 && full.listener_bytes == observation::State.live,
            "actual metric listener slots match independent ordinary requests");
    const auto attempts = observation::State.attempts;
    observation::State.fail_after = attempts;
    bool refused = false;
    try { context->AddMetricsChangedListener(listener); }
    catch (const std::bad_alloc &) { refused = true; }
    const auto refused_snapshot = context->InspectOwnedStorage();
    Require(refused && refused_snapshot.listeners == full.listeners &&
                refused_snapshot.listener_bytes == full.listener_bytes &&
                observation::State.attempts == attempts + 1 && observation::State.live == full.listener_bytes,
            "listener growth refusal keeps all previously registered listeners and slots");
    observation::State.fail_after = SIZE_MAX;
    context->AddMetricsChangedListener(listener);
    const std::array<size_t, 16> capture{};
    context->AddMetricsChangedListener([capture, &notifications](const GuiMetrics &) {
      notifications += capture[0];
    });
    const auto opaque = context->InspectOwnedStorage();
    Require(!opaque.supported && observation::State.live > opaque.listener_bytes,
            "an opaque listener can hold memory outside measured slots");
    context->ApplyUiScale(1.25f);
    Require(notifications == full.listeners + 1, "all registered inline listeners receive metrics after retry");
    context->ClearMetricsChangedListeners();
    cleared = context->InspectOwnedStorage();
    Require(cleared.supported && !cleared.listeners && cleared.listener_bytes == opaque.listener_bytes &&
                cleared.listener_bytes == observation::State.live,
            "clearing metrics callbacks retires their allocations and retains the slot vector");

    Require(context->RouteKey({GLFW_KEY_TAB}), "GUI observation creates actual focus order");
    renderer.BeginFrame(320, 240);
    for (size_t n = 0; n < 250; ++n)
    {
      renderer.PushClipRect(rect);
      renderer.DrawFilledRect(rect, color);
    }
    const auto before_query = observation::State.attempts;
    observation::State.fail_after = before_query;
    grown = context->InspectOwnedStorage();
    observed_requests = observation::State.live;
    Require(grown.supported && grown.focus_entries == 2 && grown.focus_bytes &&
                grown.vertex_bytes > 4096 && grown.clip_bytes >= 250 * sizeof(GuiRect) &&
                grown.listener_bytes + grown.focus_bytes + grown.vertex_bytes + grown.clip_bytes == observed_requests &&
                observation::State.attempts == before_query,
            "passive GUI backing query is allocation-free and matches the independent live requests");
    observation::State.fail_after = SIZE_MAX;
    renderer.EndFrame();
    context->Shutdown();
    stopped = context->InspectOwnedStorage();
    Require(stopped.supported && !stopped.vertex_bytes && !stopped.clip_bytes &&
                observation::State.live == stopped.listener_bytes + stopped.focus_bytes,
            "GUI shutdown retires renderer staging and clears focus slots");
    context.reset();
    Require(!observation::State.live && !observation::State.blocks,
            "GUI context destruction retires all observed backing");
  }
  return {{"status", "PASS"}, {"lifecycle", std::move(lifecycle)},
          {"ordinary_before_shutdown", GuiContextStorageJson(grown)},
          {"ordinary_after_shutdown", GuiContextStorageJson(stopped)},
          {"cleared_listener_bytes", cleared.listener_bytes}, {"observed_requests", observed_requests},
          {"notifications", notifications}, {"initialization_refusal_prefixes", prefixes},
          {"listener_growth_refusal", true}, {"opaque_listener_rejected", true}, {"retired", true}};
}

nlohmann::json CheckWorldProgressRequests(cutum::UApplication &app)
{
  using namespace cutum;
  using gui_context_fixture::Require;
  using Probe = ApplicationOperationSourceProbe;
  auto &context = app.GetGui();
  auto shaders = std::make_shared<UShaderManager>();
  Require(context.Initialize(shaders, {}), "progress screen storage fixture initializes GUI");
  const auto active_screen = [&]() -> UWorldProgressScreen & {
    auto *screen = dynamic_cast<UWorldProgressScreen *>(context.GetScreen());
    Require(screen && screen == Probe::Progress(app), "actual application progress alias refers to attached screen");
    return *screen;
  };
  const auto observe = [&]() { return active_screen().InspectOwnedStorage(context, &app); };
  const auto install = [&]() {
    active_screen().SetFailureActions(ApplicationScheduleRetrySaveAction{&app},
                                      ApplicationScheduleShowMenuAction{&app});
  };
  ProgressSnapshot busy, failed;
  busy.kind = WorldOperationKind::Save;
  busy.active = true;
  busy.message.assign(1200, 'p');
  busy.subDone = 7;
  busy.subTotal = 21;
  failed.kind = WorldOperationKind::Save;
  failed.succeeded = false;
  failed.errorMessage = "Save test data: " + std::string(8300, 'x');
  Probe::ShowProgress(app);
  active_screen().ApplySnapshot(failed); // warm locale and text wrapping infrastructure
  Probe::DetachProgress(app);

  WorldProgressScreenStorage built, working, error, recovered;
  size_t build_attempts = 0, build_peak = 0, peak = 0;
  {
    observation::Scope scope;
    const auto check = [&]() {
      const auto source = observe();
      Require(source.supported && source.widgets == 8 && source.requested == observation::State.live,
              "progress subtree matches independent ordinary requests");
      return source;
    };
    Probe::ShowProgress(app);
    built = check();
    build_attempts = observation::State.attempts;
    build_peak = observation::State.peak;
    active_screen().ApplySnapshot(busy);
    working = check();
    install();
    active_screen().ApplySnapshot(failed);
    error = check();
    Require(error.text_bytes > 16000 && error.callback_present[0] && error.callback_present[1] &&
                !error.callback_bytes && !active_screen().InspectOwnedStorage(context, nullptr).supported,
            "typed failure controls borrow the application and long failure text is counted");
    observation::State.fail_after = observation::State.attempts;
    const auto no_alloc = observe();
    Require(no_alloc.supported && no_alloc.requested == error.requested,
            "progress storage passive query succeeds under allocation refusal");
    observation::State.fail_after = SIZE_MAX;
    active_screen().ApplySnapshot(busy);
    recovered = check();
    peak = observation::State.peak;
    Probe::DetachProgress(app);
    Require(!observation::State.live && !observation::State.blocks && !Probe::Progress(app),
            "application detaching progress retires both widgets and owned text");
  }
  for (size_t n = 0; n < build_attempts; ++n)
  {
    observation::Scope scope(n);
    bool refused = false;
    try { Probe::ShowProgress(app); }
    catch (const std::bad_alloc &) { refused = true; }
    Require(refused && !Probe::Progress(app) && !context.GetScreen() &&
                context.InspectOwnedStorage().bindings_match && !observation::State.live,
            "each progress construction refusal clears the unpublished application binding");
    observation::State.fail_after = SIZE_MAX;
    Probe::ShowProgress(app);
    Require(observe().supported && observe().requested == observation::State.live,
            "same progress construction succeeds after allocation refusal");
    Probe::DetachProgress(app);
    Require(!observation::State.live && !observation::State.blocks, "progress retry retires all requests");
  }

  Probe::ShowProgress(app);
  install();
  const auto &slots = active_screen().GetRoot()->GetChildren().front()->GetChildren();
  auto &retry = static_cast<UGuiButton &>(*slots[4]);
  auto &back = static_cast<UGuiButton &>(*slots[5]);
  AllocatorSourcePhase phase(NewRootStorageEpoch(), 1);
  Require(retry.Activate() && app.InspectOperationStorage(phase, true).actions[0].kind ==
                                ApplicationActionKind::RetrySave,
          "retry button schedules typed save work");
  Require(back.Activate() && app.InspectOperationStorage(phase, true).actions[0].kind ==
                               ApplicationActionKind::ShowMenu,
          "back button schedules the typed menu action");
  Probe::Clear(app);
  const std::array<char, 128> capture{};
  active_screen().SetFailureActions([capture] { (void)capture; }, {});
  Require(!observe().supported && observe().bindings_match, "opaque button handler remains unsupported");
  slots[0]->AddChild(std::make_unique<UGuiWidget>());
  Require(!observe().bindings_match, "unexpected nested widget prevents complete subtree census");
  Probe::DetachProgress(app);
  context.Shutdown();
  return {{"status", "PASS"}, {"built", WorldProgressStorageJson(built)},
          {"working", WorldProgressStorageJson(working)}, {"failure", WorldProgressStorageJson(error)},
          {"recovered", WorldProgressStorageJson(recovered)}, {"build_peak", build_peak}, {"peak", peak},
          {"build_refusal_prefixes", build_attempts}, {"opaque_and_tree_controls", true},
          {"scheduled_actions", true}, {"retired", true}};
}
}
static nlohmann::json CheckDeathScreenRequests(cutum::UApplication &app)
{
  using namespace cutum;
  using Probe = ApplicationOperationSourceProbe;
  using gui_context_fixture::Require;
  using Json = nlohmann::json;

  size_t buildAttempts{}, rebuildAttempts{}, rebuildPeak{};
  DeathScreenStorage cold, expanded, rebuilt;
  const std::string cause(2048, 'r');
  UGuiContext foreignContext;
  {
    observation::Scope epoch;
    auto &screen = Probe::BuildDeath(app);
    buildAttempts = observation::State.attempts;
    cold = app.InspectDeathScreenStorage();
    Require(cold.supported && cold.present && !cold.visible && cold.widgets == 6 &&
                !cold.callback_bytes && cold.requested == observation::State.live,
            "death overlay initial ownership equality");
    Require(!screen.InspectOwnedStorage(foreignContext, app).supported,
            "death overlay rejects foreign GUI context");
    screen.SetHardcore(true);
    screen.SetCause(cause);
    expanded = app.InspectDeathScreenStorage();
    Require(expanded.supported && expanded.hardcore && expanded.text_bytes > 2048 &&
                expanded.requested == observation::State.live,
            "death overlay long caption ownership equality");

    const auto before = observation::State.attempts;
    observation::State.fail_after = before;
    const auto passive = app.InspectDeathScreenStorage();
    Require(passive.supported && observation::State.attempts == before,
            "death overlay census allocates nothing");
    observation::State.fail_after = SIZE_MAX;

    screen.Build(app.GetGui());
    rebuildAttempts = observation::State.attempts - before;
    rebuildPeak = observation::State.peak;
    rebuilt = app.InspectDeathScreenStorage();
    Require(rebuilt.supported && rebuilt.requested == observation::State.live &&
                rebuildPeak >= expanded.requested + rebuilt.requested - rebuilt.screen_bytes,
            "death overlay rebuild measures old and new tree overlap");

    size_t unknownCalls{};
    auto *respawn = static_cast<UGuiButton *>(screen.GetRoot()->GetChildren()[2].get());
    respawn->SetOnClick([&unknownCalls, data = std::array<char, 1024>{}] {
      unknownCalls += 1 + data[0];
    });
    const auto opaque = app.InspectDeathScreenStorage();
    Require(!opaque.supported && opaque.bindings_match && opaque.callback_present[0] &&
                !opaque.callback_supported[0] && !unknownCalls,
            "death overlay rejects opaque callback without executing it");
    Probe::RetireDeath(app);
    Require(!observation::State.live && !observation::State.blocks,
            "death overlay destruction retires widget, text and callback allocations");
  }

  for (size_t prefix = 0; prefix < buildAttempts; ++prefix)
  {
    observation::Scope epoch(prefix);
    bool refused = false;
    try { Probe::BuildDeath(app); }
    catch (const std::bad_alloc &) { refused = true; }
    observation::State.fail_after = SIZE_MAX;
    Require(refused && !app.InspectDeathScreenStorage().present && !observation::State.live,
            "refused death overlay never publishes a partial tree");
    Probe::BuildDeath(app);
    const auto retry = app.InspectDeathScreenStorage();
    Require(retry.supported && retry.requested == observation::State.live,
            "death overlay retries the same build after allocation refusal");
    Probe::RetireDeath(app);
    Require(!observation::State.live && !observation::State.blocks,
            "death overlay creation retry retires all requests");
  }
  for (size_t prefix = 0; prefix < rebuildAttempts; ++prefix)
  {
    observation::Scope epoch;
    auto &screen = Probe::BuildDeath(app);
    screen.SetHardcore(true);
    screen.SetCause(cause);
    const auto *oldRoot = screen.GetRoot();
    const auto oldBytes = observation::State.live;
    observation::State.fail_after = observation::State.attempts + prefix;
    bool refused = false;
    try { screen.Build(app.GetGui()); }
    catch (const std::bad_alloc &) { refused = true; }
    observation::State.fail_after = SIZE_MAX;
    const auto intact = app.InspectDeathScreenStorage();
    Require(refused && intact.supported && screen.GetRoot() == oldRoot &&
                intact.requested == oldBytes && oldBytes == observation::State.live &&
                static_cast<const UGuiLabel &>(*oldRoot->GetChildren()[1]).GetText() == cause,
            "refused death overlay rebuild keeps old tree, aliases and cause");
    screen.Build(app.GetGui());
    const auto retry = app.InspectDeathScreenStorage();
    Require(retry.supported && retry.requested == observation::State.live,
            "death overlay rebuild retry owns the replacement tree");
    Probe::RetireDeath(app);
    Require(!observation::State.live && !observation::State.blocks,
            "death overlay rebuild retry retires all requests");
  }

  return Json{{"status", "PASS"}, {"cold", DeathScreenStorageJson(cold)},
              {"expanded", DeathScreenStorageJson(expanded)}, {"rebuilt", DeathScreenStorageJson(rebuilt)},
              {"build_refusal_prefixes", buildAttempts}, {"rebuild_refusal_prefixes", rebuildAttempts},
              {"rebuild_peak", rebuildPeak}, {"passive_allocate0", true},
              {"opaque_callback_rejected", true}, {"retired", true}};
}

static void CheckDeathScreenActions()
{
  using namespace cutum;
  using Probe = ApplicationOperationSourceProbe;
  using gui_context_fixture::Require;
  auto world = std::make_shared<UWorld>(nullptr, nullptr);
  UApplication app(nullptr, world, nullptr, nullptr, nullptr, nullptr, nullptr);
  Require(app.GetGui().Initialize(std::make_shared<UShaderManager>(), {}),
          "death overlay action fixture initializes the menu renderer");
  Probe::SetGameState(app);
  world->SetDifficulty(WorldDifficulty::Normal);
  auto &initial = Probe::BuildDeath(app);
  world->SetPlayerDead(true);
  Probe::UpdateDeath(app);
  initial.OnViewportChanged(1024, 768);
  Require(initial.GetRoot()->GetBounds().X == 332 && initial.BlocksGameInput(),
          "death overlay is centered on viewport change and blocks game input");
  const auto oldRespawn = initial.GetRoot()->GetChildren()[2]->GetBounds();
  Require(app.RouteMouseButton(0, true, oldRespawn.X + 10, oldRespawn.Y + 10),
          "death overlay captures respawn press");
  auto &screen = Probe::BuildDeath(app);
  app.RouteMouseButton(0, false, oldRespawn.X + 10, oldRespawn.Y + 10);
  Require(world->IsPlayerDead(), "replacement drops old death overlay pointer capture");

  const auto click = [&](size_t index) {
    const auto bounds = screen.GetRoot()->GetChildren()[index]->GetBounds();
    Require(app.RouteMouseButton(0, true, bounds.X + 10, bounds.Y + 10) &&
                app.RouteMouseButton(0, false, bounds.X + 10, bounds.Y + 10),
            "death overlay dispatches through real application input routing");
  };
  Probe::UpdateDeath(app);
  click(2);
  Require(!world->IsPlayerDead() && !screen.BlocksGameInput(),
          "normal mode respawn clears the overlay and death state");

  world->SetPlayerDead(true);
  Probe::UpdateDeath(app);
  click(3);
  Probe::UpdateDeath(app);
  Require(world->IsPlayerDead() && !screen.BlocksGameInput() &&
              !app.AllowsWorldMousePlacement(),
          "spectator overlay stays closed while the character remains dead");

  world->SetPlayerDead(false);
  Probe::UpdateDeath(app);
  world->SetDifficulty(WorldDifficulty::Hardcore);
  world->SetHardcoreDead(true);
  Probe::UpdateDeath(app);
  Require(screen.BlocksGameInput() && !screen.GetRoot()->GetChildren()[2]->IsVisible() &&
              screen.GetRoot()->GetChildren()[4]->IsVisible(),
          "hardcore overlay offers menu and hides respawn");
  click(4);
  AllocatorSourcePhase phase(NewRootStorageEpoch(), 1);
  const auto queued = app.InspectOperationStorage(phase, true).actions[0];
  Require(Probe::IsInGame(app) && queued.Supported() &&
              queued.kind == ApplicationActionKind::ReturnMenu,
          "death overlay defers menu transition until after the input dispatch");
  phase.Close();
  Probe::ExecutePending(app);
  const auto inMenu = app.InspectDeathScreenStorage();
  Require(!Probe::IsInGame(app) && inMenu.present && inMenu.supported && !inMenu.visible &&
              app.InspectMainMenuStorage().present,
          "executed death menu action hides retained overlay and opens main menu");
}

int main(int argc,char **argv){
  using namespace cutum;using Json=nlohmann::json;const auto require=[](bool b,const char *s){if(!b)throw std::runtime_error(s);};
  try{
    const auto statusCatalog=shared_visual_source_fixture::CheckStatusCatalog(argc>1?argv[1]:"application-operation-storage.json");
    RootSourceGlContext gl;UWorld world(nullptr,nullptr);UCore core(nullptr,nullptr,nullptr,nullptr,nullptr,nullptr,nullptr);UApplication application(nullptr,nullptr,nullptr,nullptr,nullptr,nullptr,nullptr);
    UNewWorldScreen create(&application);ULoadWorldScreen load(&application);auto report=RunApplicationOperationStorageProbe(application,core,world);Json observations=Json::array();size_t prefixes{},checks{};
    CheckOriginalCaptureTypes(&create,ApplicationOperationFixtureRequest(2));
    for(int variant=0;variant<3;++variant){const auto input=ApplicationOperationFixtureRequest(variant);
      for(size_t kind=1;kind<=6;++kind){
        auto prepared=MakeApplicationActionFixture(kind,application,create,load,input); // stable source input,not measured again
        size_t attempts{},selected{};
        {observation::Scope epoch;std::function<void()> fn=prepared;const auto s=InspectApplicationAction(fn,&application,&create,&load,true);attempts=observation::State.attempts;selected=s.Total();require(s.Supported() && selected==observation::State.live,"actual callback copy ordinary request equality");++checks;
          const auto before=observation::State.attempts;observation::State.fail_after=before;const auto query=InspectApplicationAction(fn,&application,&create,&load,true);
          require(query.Supported() && query.Total()==selected && observation::State.attempts==before,"action query allocate0 under persistent refusal");observation::State.fail_after=SIZE_MAX;
          void *extra=::operator new(73);require(InspectApplicationAction(fn,&application,&create,&load,true).Total()!=observation::State.live,"unmodeled callback allocation control");::operator delete(extra);
          std::function<void()> executing=std::move(fn);require(!fn && InspectApplicationAction(executing,&application,&create,&load,true).Total()==observation::State.live,"member-to-execution move retains exact owned source");executing={};require(!observation::State.live && !observation::State.blocks,"action retirement0");}
        for(size_t prefix=0;prefix<attempts;++prefix){observation::Scope epoch;std::function<void()> destination=prepared;const auto old=InspectApplicationAction(destination,&application,&create,&load,true);const auto before=observation::State.attempts;observation::State.fail_after=before+prefix;bool refused=false;
          try{destination=prepared;}catch(const std::bad_alloc&){refused=true;}observation::State.fail_after=SIZE_MAX;
          const auto after=InspectApplicationAction(destination,&application,&create,&load,true);require(refused && after.Supported() && after.target==old.target && after.Total()==old.Total() && after.Total()==observation::State.live,"refused callback replacement preserves exact old source");
          destination=prepared;require(InspectApplicationAction(destination,&application,&create,&load,true).Total()==observation::State.live,"same-input callback retry");destination={};require(!observation::State.live && !observation::State.blocks,"callback refusal/retry retirement0");++prefixes;}
        observations.push_back({{"variant",variant},{"kind",kind},{"attempts",attempts},{"selected",selected}});
      }
      auto warm=std::make_unique<UWorldOperationRunner>(core,world);warm->Start(input);warm.reset();size_t attempts{};
      {observation::Scope epoch;auto runner=std::make_unique<UWorldOperationRunner>(core,world);const auto cold=runner->InspectOwnedStorage(&core,&world,true);require(cold.Total()==observation::State.live,"runner cold equality");++checks;
        runner->Start(input);attempts=observation::State.attempts;require(runner->InspectOwnedStorage(&core,&world,true).Total()==observation::State.live,"runner request equality");++checks;
        ApplicationOperationSourceProbe::PendingName(*runner,std::string(9000,'n'));ApplicationOperationSourceProbe::Fail(*runner,"Application source fixture");require(runner->InspectOwnedStorage(&core,&world,true).Total()==observation::State.live,"runner pending name/error equality");++checks;
        runner->Start(ApplicationOperationFixtureRequest(0));const auto retained=runner->InspectOwnedStorage(&core,&world,true);require(retained.supported && retained.Total()==observation::State.live && retained.requests[5]>0,"runner retains cleared buffer backing");++checks;
        const auto before=observation::State.attempts;observation::State.fail_after=before;const auto source=runner->InspectOwnedStorage(&core,&world,true);require(source.supported && before==observation::State.attempts,"runner query allocate0");observation::State.fail_after=SIZE_MAX;runner.reset();require(!observation::State.live && !observation::State.blocks,"runner selected retirement0");}
      for(size_t prefix=0;prefix<attempts;++prefix){observation::Scope epoch(prefix);bool refused=false;try{auto runner=std::make_unique<UWorldOperationRunner>(core,world);runner->Start(input);}catch(const std::bad_alloc&){refused=true;}
        observation::State.fail_after=SIZE_MAX;require(refused && !observation::State.live && !observation::State.blocks,"runner constructor/request refusal unwinds0");auto retry=std::make_unique<UWorldOperationRunner>(core,world);retry->Start(input);require(retry->InspectOwnedStorage(&core,&world,true).Total()==observation::State.live,"same-input runner retry equality");retry.reset();require(!observation::State.live && !observation::State.blocks,"runner refusal/retry retirement0");++prefixes;}
    }
    bool slot_cleared=false,successor_held=false;size_t invoked{};
    ApplicationOperationSourceProbe::SetPending(application,[&](){AllocatorSourcePhase phase(NewRootStorageEpoch(),1);slot_cleared=application.InspectOperationStorage(phase,true).actions[0].kind==ApplicationActionKind::Empty;++invoked;ApplicationOperationSourceProbe::SetPending(application,[&](){++invoked;});});
    ApplicationOperationSourceProbe::ExecutePending(application);AllocatorSourcePhase phase(NewRootStorageEpoch(),1);successor_held=application.InspectOperationStorage(phase,true).actions[0].callback.present;
    ApplicationOperationSourceProbe::ExecutePending(application);require(slot_cleared && successor_held && invoked==2,"actual move-before-invoke and newly scheduled successor preserved");
    for(size_t kind=1;kind<=4;++kind){ApplicationOperationSourceProbe::SetPending(application,MakeApplicationActionFixture(kind,application,create,load,ApplicationOperationFixtureRequest(0)));const auto source=application.InspectOperationStorage(phase,true);
      require(source.supported && static_cast<size_t>(source.actions[0].kind)==kind && application.InspectStoredActionCallbacks(true).rows[33].supported,"actual active application member source");}
    ApplicationOperationSourceProbe::Clear(application);phase.Close();require(!application.InspectOperationStorage(phase,true).supported,"actual closed application source phase");
    report["ordinary_cases"]=observations;report["ordinary_equality_checks"]=checks;report["refusal_prefixes"]=prefixes;report["query_allocate0"]=true;report["move_before_invoke_successor"]=true;report["selected_final_live"]=observation::State.live;
    report["gui_context_storage"]=CheckGuiContextRequests();
    report["world_progress_storage"]=CheckWorldProgressRequests(application);
    report["main_menu_storage"]=CheckMainMenuRequests(application);
    report["hud_storage"]=CheckInGameHudRequests();
    report["console_storage"]=CheckConsoleRequests(argc>1?argv[1]:"application-operation-storage.json");
    report["death_screen_storage"]=CheckDeathScreenRequests(application);
    CheckDeathScreenActions();
    report["death_screen_storage"]["action_lifecycle"]=true;
    report["player_window_storage"]=CheckPlayerWindowRequests();
    report["character_page_storage"]=character_page_fixture::Run();
    report["palette_grid_storage"]=CheckPaletteGridRequests();
    report["palette_shell_storage"]=palette_shell_fixture::Run();
    report["icon_lifecycle"]=icon_lifecycle_fixture::Run(argc>1?argv[1]:"application-operation-storage.json");
    report["character_snapshot_storage"]=character_snapshot_fixture::Run();
    report["crafting_storage"]=crafting_storage_fixture::Run();
    report["anvil_storage"]=anvil_storage_fixture::Run();
    report["worldgen_palette_storage"]=worldgen_palette_storage_fixture::Run();
    report["worldgen_palette_owner"]=worldgen_palette_storage_fixture::RunOwnerCensus();
    report["worldgen_palette_compact"]=worldgen_palette_storage_fixture::RunCompactLayout();
    report["content_preview_owner"]=shared_content_preview_fixture::Run(argc>1?argv[1]:"application-operation-storage.json");
    report["object_icon_owner"]=object_icon_storage_fixture::Run(argc>1?argv[1]:"application-operation-storage.json");
    report["inventory_icon_service_owner"]=inventory_icon_storage_fixture::Run(argc>1?argv[1]:"application-operation-storage.json");
    report["inventory_icon_codec_owner"]=inventory_icon_codec_fixture::Run(argc>1?argv[1]:"application-operation-storage.json");
    report["creature_item_icon_owner"]=creature_item_icon_fixture::Run(argc>1?argv[1]:"application-operation-storage.json");
    report["status_catalog_owner"]=std::move(statusCatalog);
    report["shared_visual_sources"]=shared_visual_source_fixture::CheckVisualSources();
    report["rigid_model_source"]=shared_visual_source_fixture::CheckRigidModels(argc>1?argv[1]:"application-operation-storage.json");
    report["retained_visual_assets"]=shared_visual_source_fixture::CheckRetainedVisualOwners();
    report["visual_loader_construction"]=shared_visual_source_fixture::CheckVisualLoaderConstruction(argc>1?argv[1]:"application-operation-storage.json");
    report["skeleton_mesh_construction"]=shared_visual_source_fixture::CheckSkeletonMeshConstruction();
    report["shared_texture_sources"]=shared_texture_source_fixture::Run(argc>1?argv[1]:"application-operation-storage.json");
    std::ofstream file(argc>1?argv[1]:"application-operation-storage.json");file<<report.dump(2)<<'\n';require(bool(file),"application operation report write");std::cout<<report.dump()<<'\n';return 0;
  }catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}
}
