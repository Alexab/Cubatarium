#include "Audio/AudioDirector.h"

#include "Audio/AudioApi.h"
#include "App/Platform/Log.h"
#include "ThirdParty/miniaudio.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstring>
#include <numeric>

namespace cutum
{

/// Pair of streaming voices used for crossfading one continuous source
/// (music playlist / biome loop / weather overlay). Audio thread only.
struct StreamingLoop
{
  ma_sound Slots[2]{};
  bool Ready[2]{false, false};
  int Active{-1};
  int FadingIn{-1};
  int FadingOut{-1};
  float Fade{0.f};
  std::string SetName; // current source identity (set name or track path)
  /// Last source that failed to open. Stops a missing file from being
  /// probed again on every housekeeping tick.
  std::string MissName;
  float BaseGain{1.f};

  void FreeSlot(int index)
  {
    if (Ready[index])
    {
      ma_sound_stop(&Slots[index]);
      ma_sound_uninit(&Slots[index]);
      Ready[index] = false;
    }
  }
};

/// Streaming music/loop voices + schedulers; all miniaudio objects stay
/// in this struct (audio thread only).
struct UAudioDirector::HousekeepingState
{
  /// Music playlist crossfade pair (non-looping tracks).
  StreamingLoop Music;
  std::vector<std::string> Playlist;
  std::vector<int> PlayOrder;
  std::size_t PlayPos{0};
  std::string Context;
  /// Relative paths last seen under the music root.
  std::vector<std::string> ScannedFiles;
  bool MusicScanned{false};
  float NextMusicScanSec{0.f};
  /// Open failed for every track; wait for the next scan instead of looping.
  bool MusicGiveUp{false};
  std::vector<std::string> Unopenable;

  /// Continuous ambience layers.
  StreamingLoop BiomeLoop;
  StreamingLoop WeatherLoop;

  /// Cached stinger bank ids for the current biome snapshot.
  std::vector<std::uint32_t> DayStingers;
  std::vector<std::uint32_t> NightStingers;
  std::string StingerBiome;
  /// Last logged biome (diagnostics; also gates the log rate).
  std::string LastBiomeId;
  float NextStingerSec{0.f};
  std::string LastWeatherName;
};

namespace
{

/// Housekeeping cadence for ambience/music scheduling (block in seconds).
constexpr std::uint32_t kIdleTickMs = 100;
/// How often an empty music context rechecks the disk for new files.
constexpr float kMusicRescanSec = 3.f;

bool IsMusicFile(const std::filesystem::path &path)
{
  std::string ext = path.extension().string();
  for (char &ch : ext)
  {
    ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
  }
  return ext == ".mp3" || ext == ".wav" || ext == ".flac";
}

std::vector<std::string> ListMusicFiles(const std::filesystem::path &root)
{
  std::vector<std::string> files;
  std::error_code error;
  if (!std::filesystem::exists(root, error) || error)
  {
    return files;
  }
  std::filesystem::recursive_directory_iterator it(
      root, std::filesystem::directory_options::skip_permission_denied, error);
  const std::filesystem::recursive_directory_iterator end;
  for (; !error && it != end; it.increment(error))
  {
    std::error_code fileError;
    if (!it->is_regular_file(fileError) || fileError || !IsMusicFile(it->path()))
    {
      continue;
    }
    const std::filesystem::path relative =
        std::filesystem::relative(it->path(), root, fileError);
    if (fileError)
    {
      continue;
    }
    files.push_back(relative.generic_string());
  }
  std::sort(files.begin(), files.end());
  return files;
}

bool ContainsPath(const std::vector<std::string> &paths, const std::string &path)
{
  return std::find(paths.begin(), paths.end(), path) != paths.end();
}

void AdvanceLoopFade(StreamingLoop &loop, float fadeStep)
{
  if (loop.FadingOut < 0)
  {
    return;
  }
  loop.Fade = std::min(1.f, loop.Fade + fadeStep);
  const float outGain = loop.BaseGain * (1.f - loop.Fade);
  const float inGain = loop.BaseGain * loop.Fade;
  if (loop.FadingOut >= 0 && loop.Ready[loop.FadingOut])
  {
    ma_sound_set_volume(&loop.Slots[loop.FadingOut], outGain);
  }
  if (loop.FadingIn >= 0 && loop.Ready[loop.FadingIn])
  {
    ma_sound_set_volume(&loop.Slots[loop.FadingIn], inGain);
  }
  if (loop.Fade >= 1.f)
  {
    const int done = loop.FadingOut;
    loop.FadingOut = -1;
    loop.FadingIn = -1;
    if (done >= 0)
    {
      loop.FreeSlot(done);
      if (loop.Active == done)
      {
        loop.Active = loop.Ready[1 - done] ? 1 - done : -1;
      }
    }
  }
}

bool StartLoopCrossfade(UAudioEngine &engine, ma_engine *maEngine,
                        StreamingLoop &loop, const std::string &setName,
                        const std::filesystem::path &file, bool looping,
                        float gain, AudioBus bus)
{
  if (loop.SetName == setName &&
      (loop.Active >= 0 || loop.FadingIn >= 0 || loop.FadingOut >= 0))
  {
    return true;
  }
  if (loop.MissName == setName)
  {
    return false;
  }
  const int freeSlot = loop.Active < 0 ? 0 : 1 - loop.Active;
  loop.FreeSlot(freeSlot);
  ma_sound_group *group = static_cast<ma_sound_group *>(engine.GetRawGroup(bus));
  const ma_sound_flags flags = static_cast<ma_sound_flags>(
      MA_SOUND_FLAG_STREAM | MA_SOUND_FLAG_NO_SPATIALIZATION);
  const ma_result result =
      ma_sound_init_from_file(maEngine, file.string().c_str(), flags, group,
                              nullptr, &loop.Slots[freeSlot]);
  if (result != MA_SUCCESS)
  {
    loop.Ready[freeSlot] = false;
    loop.MissName = setName;
    return false;
  }
  loop.Ready[freeSlot] = true;
  loop.MissName.clear();
  ma_sound_set_looping(&loop.Slots[freeSlot], looping ? MA_TRUE : MA_FALSE);
  loop.BaseGain = gain;
  ma_sound_set_volume(&loop.Slots[freeSlot], loop.Active >= 0 ? 0.f : gain);
  ma_sound_set_start_time_in_pcm_frames(&loop.Slots[freeSlot],
                                        ma_engine_get_time(maEngine));
  ma_sound_start(&loop.Slots[freeSlot]);
  if (loop.Active >= 0 && loop.Ready[loop.Active])
  {
    loop.FadingOut = loop.Active;
    loop.FadingIn = freeSlot;
    loop.Fade = 0.f;
  }
  else
  {
    loop.Active = freeSlot;
    loop.FadingOut = -1;
    loop.FadingIn = -1;
  }
  loop.SetName = setName;
  return true;
}

/// Fades a loop out from its current gain and clears its source.
void StopStreamingLoop(StreamingLoop &loop)
{
  if (loop.SetName.empty())
  {
    loop.MissName.clear();
    return;
  }
  if (loop.Active >= 0 && loop.FadingOut < 0)
  {
    loop.FadingOut = loop.Active;
    loop.FadingIn = -1;
    loop.Fade = 0.f;
  }
  else if (loop.FadingOut < 0)
  {
    loop.SetName.clear();
    loop.MissName.clear();
    loop.FreeSlot(0);
    loop.FreeSlot(1);
    loop.Active = -1;
  }
}

/// Seconds between retries after a failed device open (no-device machines).
constexpr float kDeviceRetrySeconds = 5.f;
/// Random one-shot ambience window (birds, drips — the "living world" layer).
constexpr float kStingerMinSec = 15.f;
constexpr float kStingerMaxSec = 180.f;

bool IsMaterialEvent(AudioEventKind kind)
{
  switch (kind)
  {
  case AudioEventKind::Footstep:
  case AudioEventKind::Land:
  case AudioEventKind::DigHit:
  case AudioEventKind::DigComplete:
  case AudioEventKind::Place:
  case AudioEventKind::Splash:
    return true;
  default:
    return false;
  }
}

} // namespace

namespace
{

/// Event.Archetype carries the raw LocomotionArchetype value
/// (Creatures/Locomotion/LocomotionTypes.h) to keep Audio decoupled from
/// creature headers. Order must match that enum.
const char *ArchetypeKey(std::uint8_t value)
{
  switch (value)
  {
  case 0:
    return "terrestrial_biped";
  case 1:
    return "terrestrial_quadruped";
  case 2:
    return "aerial";
  case 3:
    return "aquatic";
  case 4:
    return "serpentine";
  default:
    return "";
  }
}

const CreatureSoundDefaultDef *
FindCreatureFlavor(std::uint8_t archetype)
{
  if (archetype == 255)
  {
    return nullptr;
  }
  const UMaterialSoundMap *materialMap = GetAudioMaterialMap();
  return materialMap
             ? materialMap->FindCreatureDefault(ArchetypeKey(archetype))
             : nullptr;
}

/// Last-resort set per event kind when no material/creature mapping hit —
/// keeps generic gameplay feedback (UI, hurt, swing) alive without per-call
/// material resolution.
const char *KindFallbackSet(AudioEventKind kind)
{
  switch (kind)
  {
  case AudioEventKind::Footstep:
  case AudioEventKind::Land:
    return "footstep.grass";
  case AudioEventKind::DigHit:
    return "dig.soft";
  case AudioEventKind::DigComplete:
    return "dug.soft";
  case AudioEventKind::Place:
    return "place.block";
  case AudioEventKind::MeleeSwing:
    return "melee.swing";
  case AudioEventKind::MeleeHit:
    return "melee.hit";
  case AudioEventKind::RangedShot:
    return "ranged.shot";
  case AudioEventKind::Eat:
    return "eat.food";
  case AudioEventKind::Pickup:
    return "pickup.item";
  case AudioEventKind::Equip:
  case AudioEventKind::Unequip:
    return "equip.armor";
  case AudioEventKind::UiClick:
    return "ui.click";
  case AudioEventKind::UiOpen:
    return "ui.open";
  case AudioEventKind::UiClose:
    return "ui.close";
  case AudioEventKind::CreatureHurt:
    return "creature.hurt";
  case AudioEventKind::CreatureDeath:
    return "creature.death";
  case AudioEventKind::Splash:
    return "splash.water";
  }
  return "";
}

} // namespace

UAudioDirector::~UAudioDirector()
{
  if (Running.load())
  {
    Stop();
  }
  ResetHousekeeping();
}

void UAudioDirector::Start(const AudioSettings &settings,
                           const std::filesystem::path &contentAudioDir,
                           const std::filesystem::path &soundsRoot,
                           const std::filesystem::path &musicRoot)
{
  if (Running.load())
  {
    return;
  }
  {
    std::lock_guard<std::mutex> lock(SettingsMutex);
    SettingsSnapshot = ClampAudioSettings(settings);
  }
  ContentAudioDir = contentAudioDir;
  SoundsRoot = soundsRoot;
  MusicRoot = musicRoot;
  ContentLoaded = false;
  UAudioApi::Bind(&Queue);
  Running.store(true);
  Worker = std::thread([this]() { Run(); });
}

void UAudioDirector::Stop()
{
  if (!Running.load())
  {
    return;
  }
  Running.store(false);
  if (Worker.joinable())
  {
    Worker.join();
  }
  ResetHousekeeping();
  UAudioApi::Bind(nullptr);
}

void UAudioDirector::ApplySettings(const AudioSettings &settings)
{
  {
    std::lock_guard<std::mutex> lock(SettingsMutex);
    SettingsSnapshot = ClampAudioSettings(settings);
  }
  // Engine volume setters are thread-safe; the ~10 Hz tick picks up the
  // Enabled flag and lifecycle changes.
  ApplyEffectiveVolumes();
}

void UAudioDirector::SetFocusMuted(bool muted)
{
  FocusMuted.store(muted);
  ApplyEffectiveVolumes();
}

void UAudioDirector::ApplyEffectiveVolumes()
{
  AudioSettings effective;
  {
    std::lock_guard<std::mutex> lock(SettingsMutex);
    effective = SettingsSnapshot;
  }
  if (FocusMuted.load())
  {
    effective.Enabled = false;
  }
  Engine.ApplyVolumes(effective);
}

AudioSettings UAudioDirector::ReadSettingsSnapshot() const
{
  std::lock_guard<std::mutex> lock(SettingsMutex);
  return SettingsSnapshot;
}

void UAudioDirector::LoadContent()
{
  ContentLoaded = true;
  if (!Manifest.LoadFromFile((ContentAudioDir / "sounds.json").string()))
  {
    return;
  }
  LoadAudioMaterialMap(
      (ContentAudioDir / "material_sounds.json").string());
  if (!MusicManifest.LoadFromFile(
          (ContentAudioDir / "music_manifest.json").string()))
  {
    CubatariumLogInfo("Audio",
                      "music: no manifest — running without music");
  }

  const std::size_t usableSets =
      Bank.Init(&Engine, Manifest, SoundsRoot);
  CubatariumLogInfo("Audio",
                    "sound bank: " + std::to_string(usableSets) + "/" +
                        std::to_string(Manifest.GetSetCount()) +
                        " sets with files");
  if (!Hk)
  {
    Hk = new HousekeepingState();
  }
}

void UAudioDirector::EnsureEngine(const AudioSettings &settings)
{
  if (settings.Enabled)
  {
    if (Engine.IsReady())
    {
      return;
    }
    if (DeviceRetryCooldownSec > 0.f)
    {
      return;
    }
    if (!Engine.Init(settings))
    {
      // Headless machine / no device: retry rarely, keep the game running.
      DeviceRetryCooldownSec = kDeviceRetrySeconds;
      return;
    }
    if (!ContentLoaded)
    {
      LoadContent();
    }
    return;
  }
  if (Engine.IsReady())
  {
    ResetHousekeeping();
    Engine.Shutdown();
    ContentLoaded = false;
  }
}

std::uint32_t UAudioDirector::ResolveEventSet(const UAudioEvent &event) const
{
  if (IsMaterialEvent(event.Kind) && event.MaterialId != 0)
  {
    const UMaterialSoundMap *materialMap = GetAudioMaterialMap();
    const SurfaceMaterialDef *material =
        materialMap ? materialMap->GetMaterial(event.MaterialId) : nullptr;
    if (material)
    {
      std::string setName;
      switch (event.Kind)
      {
      case AudioEventKind::Footstep:
      case AudioEventKind::Land:
      case AudioEventKind::Splash:
        setName = material->FootstepSet;
        break;
      case AudioEventKind::DigHit:
        setName = material->DigSet;
        break;
      case AudioEventKind::DigComplete:
      case AudioEventKind::Place:
        setName = material->DugSet.empty() ? material->DigSet
                                           : material->DugSet;
        break;
      default:
        break;
      }
      if (!setName.empty())
      {
        return Bank.ResolveId(setName);
      }
    }
    // Fall through to the creature default set for unknown surfaces.
    if (event.Kind != AudioEventKind::Footstep &&
        event.Kind != AudioEventKind::Land &&
        event.Kind != AudioEventKind::Splash)
    {
      return 0;
    }
  }
  if ((event.Kind == AudioEventKind::Footstep ||
       event.Kind == AudioEventKind::Land) &&
      event.Archetype != 255)
  {
    const CreatureSoundDefaultDef *def = FindCreatureFlavor(event.Archetype);
    if (def && !def->FootstepSet.empty())
    {
      return Bank.ResolveId(def->FootstepSet);
    }
  }
  return event.SetId;
}

float UAudioDirector::SampleJitter() { return Jitter(Random); }

void UAudioDirector::Run()
{
  CubatariumSetWorkerJobKind("AudioDirector");
  const std::chrono::steady_clock::time_point start =
      std::chrono::steady_clock::now();
  std::vector<UAudioEvent> drained;
  while (Running.load())
  {
    NowSec = std::chrono::duration<float>(
                 std::chrono::steady_clock::now() - start)
                 .count();
    const AudioSettings settings = ReadSettingsSnapshot();
    EnsureEngine(settings);
    if (Engine.IsReady())
    {
      Engine.SetListener(UAudioApi::ReadListener());
    }

    const bool hasEvents = Queue.DrainWait(drained, kIdleTickMs);
    if (hasEvents)
    {
      for (const UAudioEvent &event : drained)
      {
        HandleEvent(event, settings);
      }
      // Keep crossfades/stingers advancing even during event bursts; the
      // tick is cheap and must not starve behind footsteps.
      TickHousekeeping(static_cast<float>(kIdleTickMs) / 1000.f);
      continue;
    }

    // Idle tick: ambience/music scheduling + device retry cooldown.
    if (DeviceRetryCooldownSec > 0.f)
    {
      DeviceRetryCooldownSec -= static_cast<float>(kIdleTickMs) / 1000.f;
    }
    TickHousekeeping(static_cast<float>(kIdleTickMs) / 1000.f);
  }
  ResetHousekeeping();
  if (Engine.IsReady())
  {
    Engine.Shutdown();
  }
}

void UAudioDirector::HandleEvent(const UAudioEvent &event,
                                 const AudioSettings & /*settings*/)
{
  if (!Engine.IsReady() || !Bank.IsReady())
  {
    return;
  }
  const std::uint32_t bankId = [&]()
  {
    std::uint32_t id = ResolveEventSet(event);
    if (id == 0)
    {
      id = Bank.ResolveId(KindFallbackSet(event.Kind));
    }
    return id;
  }();
  if (bankId == 0)
  {
    // Unresolved sets (no SoundBank entry / mapping) drop silently: gameplay
    // must never pay for audio content gaps.
    return;
  }
  // Creature flavor reshapes the surface sound (heavier bodies pitch down
  // and repeat slower) — the "sound matches creature AND surface" rule.
  float pitchMul = 1.f;
  float intervalMul = 1.f;
  if (const CreatureSoundDefaultDef *flavor =
          FindCreatureFlavor(event.Archetype))
  {
    pitchMul = flavor->PitchMul;
    intervalMul = flavor->IntervalMul;
  }
  if (Bank.IsOnCooldown(bankId, NowSec, intervalMul))
  {
    return;
  }
  const float pitch = pitchMul * (1.f + SampleJitter());
  const float gain = 1.f + SampleJitter();
  Bank.Play(bankId, event.X, event.Y, event.Z, pitch, gain, event.Strength);
  Bank.MarkPlayed(bankId, NowSec);
  EventsHandled.fetch_add(1);
}

void UAudioDirector::TickMusic(const AudioAmbienceSnapshot &amb, float fadeStep)
{
  ma_engine *maEngine = static_cast<ma_engine *>(Engine.GetRawEngine());
  if (!maEngine || !Hk)
  {
    return;
  }
  // Music never stands in for a biome bed: an empty context fades the
  // music bus out and leaves ambience running on its own bus.
  const std::string biomeId =
      amb.Valid && amb.InGame ? std::string(amb.BiomeId) : std::string();
  const bool isNight = amb.Valid && amb.InGame && amb.IsNight;
  MusicSelection selection =
      MusicManifest.Select(Hk->ScannedFiles, biomeId, isNight);
  const bool contextChanged = selection.Context != Hk->Context;
  const bool scanDue = !Hk->MusicScanned || NowSec >= Hk->NextMusicScanSec;
  if ((contextChanged || !Hk->MusicScanned ||
       (selection.Files.empty() && scanDue)) &&
      (contextChanged || scanDue))
  {
    Hk->ScannedFiles = ListMusicFiles(MusicRoot);
    Hk->MusicScanned = true;
    Hk->NextMusicScanSec = NowSec + kMusicRescanSec;
    selection = MusicManifest.Select(Hk->ScannedFiles, biomeId, isNight);
  }
  const bool selectionChanged =
      selection.Context != Hk->Context || selection.Files != Hk->Playlist;
  if (selectionChanged)
  {
    Hk->Context = selection.Context;
    Hk->Playlist = selection.Files;
    Hk->PlayOrder.resize(Hk->Playlist.size());
    std::iota(Hk->PlayOrder.begin(), Hk->PlayOrder.end(), 0);
    if (!Hk->PlayOrder.empty())
    {
      std::shuffle(Hk->PlayOrder.begin(), Hk->PlayOrder.end(), Random);
    }
    Hk->PlayPos = 0;
    Hk->MusicGiveUp = false;
    Hk->Unopenable.clear();
    CubatariumLogInfo("Audio", "music: context '" + Hk->Context +
                                   "' tracks=" +
                                   std::to_string(Hk->Playlist.size()));
    if (Hk->Playlist.empty())
    {
      StopStreamingLoop(Hk->Music);
    }
  }
  if (!Hk->Playlist.empty() && !Hk->MusicGiveUp)
  {
    const bool finished =
        Hk->Music.Active >= 0 && Hk->Music.Ready[Hk->Music.Active] &&
        ma_sound_at_end(&Hk->Music.Slots[Hk->Music.Active]);
    const bool idle = Hk->Music.Active < 0 && Hk->Music.FadingOut < 0 &&
                      Hk->Music.FadingIn < 0;
    if (selectionChanged || finished || idle)
    {
      bool started = false;
      for (std::size_t attempt = 0; attempt < Hk->Playlist.size(); ++attempt)
      {
        if (Hk->PlayPos >= Hk->PlayOrder.size())
        {
          std::shuffle(Hk->PlayOrder.begin(), Hk->PlayOrder.end(), Random);
          if (Hk->PlayOrder.size() > 1 &&
              Hk->Music.SetName == Hk->Playlist[Hk->PlayOrder.front()])
          {
            std::swap(Hk->PlayOrder.front(), Hk->PlayOrder.back());
          }
          Hk->PlayPos = 0;
        }
        const std::string &trackFile = Hk->Playlist[Hk->PlayOrder[Hk->PlayPos]];
        ++Hk->PlayPos;
        if (ContainsPath(Hk->Unopenable, trackFile))
        {
          continue;
        }
        if (StartLoopCrossfade(Engine, maEngine, Hk->Music, trackFile,
                               MusicRoot / trackFile, false, 1.f,
                               AudioBus::Music))
        {
          started = true;
          break;
        }
        Hk->Unopenable.push_back(trackFile);
        CubatariumLogInfo("Audio", "music: failed to open " + trackFile);
      }
      if (!started)
      {
        Hk->MusicGiveUp = true;
        CubatariumLogInfo("Audio", "music: context '" + Hk->Context +
                                       "' has no playable files");
        StopStreamingLoop(Hk->Music);
      }
    }
  }
  else if (Hk->Playlist.empty())
  {
    StopStreamingLoop(Hk->Music);
  }
  AdvanceLoopFade(Hk->Music, fadeStep);
}

void UAudioDirector::TickHousekeeping(float dt)
{
  if (!Engine.IsReady() || !Hk)
  {
    return;
  }
  ma_engine *maEngine = static_cast<ma_engine *>(Engine.GetRawEngine());
  if (!maEngine)
  {
    return;
  }

  const AudioAmbienceSnapshot amb = UAudioApi::ReadAmbience();
  const float crossfadeSec =
      std::max(0.5f, MusicManifest.GetCrossfadeSeconds());
  const float fadeStep = dt / crossfadeSec;
  TickMusic(amb, fadeStep);

  // ---- Biome ambience loop ----
  {
    const UMaterialSoundMap *materialMap = GetAudioMaterialMap();
    std::string targetSet;
    float targetGain = 0.f;
    // Menu / not in game: no world ambience at all — silence, not fallback.
    if (materialMap && amb.Valid && amb.InGame)
    {
      const BiomeAmbienceDef *biome =
          materialMap->ResolveBiome(std::string(amb.BiomeId));
      if (biome && !biome->LoopSet.empty())
      {
        if (const SoundSetDef *set = Manifest.FindByName(biome->LoopSet))
        {
          if (!set->Files.empty())
          {
            targetSet = biome->LoopSet;
            targetGain = set->BaseGain;
          }
        }
      }
      if (std::string(amb.BiomeId) != Hk->LastBiomeId)
      {
        Hk->LastBiomeId = amb.BiomeId;
        CubatariumLogInfo(
            "Audio", std::string("biome '") + amb.BiomeId + "' ambience: " +
                         (targetSet.empty() ? std::string("(none)")
                                            : targetSet));
      }
    }
    if (!targetSet.empty())
    {
      const SoundSetDef *set = Manifest.FindByName(targetSet);
      StartLoopCrossfade(Engine, maEngine, Hk->BiomeLoop, targetSet,
                         SoundsRoot / set->Files.front(), true, targetGain,
                         AudioBus::Ambient);
    }
    else
    {
      StopStreamingLoop(Hk->BiomeLoop);
    }
    AdvanceLoopFade(Hk->BiomeLoop, fadeStep);
  }

  // ---- Weather overlay loop ----
  {
    const UMaterialSoundMap *materialMap = GetAudioMaterialMap();
    std::string targetSet;
    float targetGain = 0.f;
    if (materialMap && amb.Valid && amb.InGame)
    {
      const std::string weatherName(amb.WeatherName);
      if (const WeatherAmbienceDef *weather =
              materialMap->FindWeather(weatherName))
      {
        if (!weather->LoopSet.empty())
        {
          if (const SoundSetDef *set = Manifest.FindByName(weather->LoopSet))
          {
            if (!set->Files.empty())
            {
              targetSet = weather->LoopSet;
              targetGain = set->BaseGain;
            }
          }
        }
      }
    }
    if (!targetSet.empty())
    {
      const SoundSetDef *set = Manifest.FindByName(targetSet);
      StartLoopCrossfade(Engine, maEngine, Hk->WeatherLoop, targetSet,
                         SoundsRoot / set->Files.front(), true, targetGain,
                         AudioBus::Ambient);
    }
    else if (!Hk->WeatherLoop.SetName.empty())
    {
      // Weather cleared / left the world: fade the overlay out.
      StopStreamingLoop(Hk->WeatherLoop);
    }
    AdvanceLoopFade(Hk->WeatherLoop, fadeStep);
  }

  // ---- Random stingers (birds by day, crickets/drips by night) ----
  {
    const UMaterialSoundMap *materialMap = GetAudioMaterialMap();
    const std::string biomeKey =
        amb.Valid && amb.InGame ? std::string(amb.BiomeId) : std::string();
    if (biomeKey != Hk->StingerBiome)
    {
      Hk->StingerBiome = biomeKey;
      Hk->DayStingers.clear();
      Hk->NightStingers.clear();
      if (materialMap)
      {
        const BiomeAmbienceDef *biome = materialMap->ResolveBiome(biomeKey);
        if (biome)
        {
          for (const std::string &name : biome->DayStingers)
          {
            const std::uint32_t id = Bank.ResolveId(name);
            if (id != 0)
            {
              Hk->DayStingers.push_back(id);
            }
          }
          for (const std::string &name : biome->NightStingers)
          {
            const std::uint32_t id = Bank.ResolveId(name);
            if (id != 0)
            {
              Hk->NightStingers.push_back(id);
            }
          }
        }
      }
    }
    if (NowSec >= Hk->NextStingerSec)
    {
      std::uniform_real_distribution<float> window(kStingerMinSec,
                                                   kStingerMaxSec);
      Hk->NextStingerSec = NowSec + window(Random);
      const std::vector<std::uint32_t> &pool =
          amb.IsNight ? Hk->NightStingers : Hk->DayStingers;
      if (!pool.empty())
      {
        const std::uint32_t id =
            pool[std::uniform_int_distribution<std::size_t>(
                0, pool.size() - 1)(Random)];
        const AudioListenerSnapshot listener = UAudioApi::ReadListener();
        std::uniform_real_distribution<float> offset(-8.f, 8.f);
        Bank.Play(id, listener.X + offset(Random),
                  listener.Y + offset(Random) * 0.25f,
                  listener.Z + offset(Random), 1.f, 1.f, 1.f);
      }
    }
  }
}

void UAudioDirector::ResetHousekeeping()
{
  if (Hk)
  {
    Hk->Music.FreeSlot(0);
    Hk->Music.FreeSlot(1);
    Hk->BiomeLoop.FreeSlot(0);
    Hk->BiomeLoop.FreeSlot(1);
    Hk->WeatherLoop.FreeSlot(0);
    Hk->WeatherLoop.FreeSlot(1);
    delete Hk;
    Hk = nullptr;
  }
}

} // namespace cutum
