#include "Audio/AudioEngine.h"

#include "App/Platform/Log.h"
#include "ThirdParty/miniaudio.h"

#include <iostream>

namespace cutum
{

namespace
{

// Bus indices inside MiniaudioState::Groups; order matches AudioBus.
constexpr int kBusCount = 4;
constexpr int kMusicBus = 0;
constexpr int kAmbientBus = 1;
constexpr int kSfxBus = 2;
constexpr int kUiBus = 3;

int BusIndex(AudioBus bus)
{
  switch (bus)
  {
  case AudioBus::Music:
    return kMusicBus;
  case AudioBus::Ambient:
    return kAmbientBus;
  case AudioBus::Sfx:
    return kSfxBus;
  case AudioBus::Ui:
    return kUiBus;
  }
  return kSfxBus;
}

/// Concurrency is capped by the SoundBank's per-set voice pools; the engine
/// config in 0.11.26 has no global voice limit to tune.

} // namespace

struct UAudioEngine::MiniaudioState
{
  ma_resource_manager ResourceManager{};
  bool ResourceManagerReady{false};
  ma_engine Engine{};
  bool EngineReady{false};
  ma_sound_group Groups[kBusCount]{};
  bool GroupsReady[kBusCount]{};
};

UAudioEngine::~UAudioEngine()
{
  if (Ready)
  {
    Shutdown();
  }
}

bool UAudioEngine::Init(const AudioSettings &settings)
{
  if (Ready)
  {
    return true;
  }
  State = new MiniaudioState();

  ma_resource_manager_config rmConfig = ma_resource_manager_config_init();
  ma_result result =
      ma_resource_manager_init(&rmConfig, &State->ResourceManager);
  if (result != MA_SUCCESS)
  {
    std::cerr << "Audio: resource manager init failed (" << result << ")"
              << std::endl;
    CubatariumLogError("Audio",
                       "resource manager init failed: " + std::to_string(result));
    delete State;
    State = nullptr;
    return false;
  }
  State->ResourceManagerReady = true;

  ma_engine_config engineConfig = ma_engine_config_init();
  engineConfig.pResourceManager = &State->ResourceManager;
  result = ma_engine_init(&engineConfig, &State->Engine);
  if (result != MA_SUCCESS)
  {
    std::cerr << "Audio: engine init failed (" << result
              << ") — running without audio device" << std::endl;
    CubatariumLogError("Audio", "engine init failed: " + std::to_string(result));
    ma_resource_manager_uninit(&State->ResourceManager);
    delete State;
    State = nullptr;
    return false;
  }
  State->EngineReady = true;

  // Leaf bus groups route straight into the engine endpoint; the master
  // slider lives on ma_engine_set_volume (the endpoint group itself).
  for (int i = 0; i < kBusCount; ++i)
  {
    result = ma_sound_group_init(&State->Engine, 0, nullptr, &State->Groups[i]);
    if (result != MA_SUCCESS)
    {
      CubatariumLogError("Audio",
                         "sound group init failed: " + std::to_string(result));
      break;
    }
    State->GroupsReady[i] = true;
  }

  {
    std::lock_guard<std::mutex> lock(VolumesMutex);
    Volumes = ClampAudioSettings(settings);
  }
  ApplyVolumes(settings);
  Ready = true;
  CubatariumLogInfo("Audio",
                    std::string("device ready, backend=") + GetBackendName());
  return true;
}

void UAudioEngine::Shutdown()
{
  if (!State)
  {
    return;
  }
  for (int i = 0; i < kBusCount; ++i)
  {
    if (State->GroupsReady[i])
    {
      ma_sound_group_uninit(&State->Groups[i]);
      State->GroupsReady[i] = false;
    }
  }
  if (State->EngineReady)
  {
    ma_engine_uninit(&State->Engine);
    State->EngineReady = false;
  }
  if (State->ResourceManagerReady)
  {
    ma_resource_manager_uninit(&State->ResourceManager);
    State->ResourceManagerReady = false;
  }
  delete State;
  State = nullptr;
  Ready = false;
}

void UAudioEngine::ApplyVolumes(const AudioSettings &settings)
{
  const AudioSettings clamped = ClampAudioSettings(settings);
  {
    std::lock_guard<std::mutex> lock(VolumesMutex);
    Volumes = clamped;
  }
  if (!State || !State->EngineReady)
  {
    return;
  }
  const float master = clamped.Enabled ? clamped.Master : 0.f;
  ma_engine_set_volume(&State->Engine, master);
  const float busGains[kBusCount] = {clamped.Music, clamped.Ambient, clamped.Sfx,
                                     clamped.Ui};
  for (int i = 0; i < kBusCount; ++i)
  {
    if (State->GroupsReady[i])
    {
      ma_sound_group_set_volume(&State->Groups[i], busGains[i]);
    }
  }
}

void UAudioEngine::SetListener(const AudioListenerSnapshot &snapshot)
{
  if (!State || !State->EngineReady || !snapshot.Valid)
  {
    return;
  }
  ma_engine_listener_set_position(&State->Engine, 0, snapshot.X, snapshot.Y,
                                  snapshot.Z);
  ma_engine_listener_set_direction(&State->Engine, 0, snapshot.FwdX,
                                   snapshot.FwdY, snapshot.FwdZ);
  ma_engine_listener_set_world_up(&State->Engine, 0, snapshot.UpX, snapshot.UpY,
                                  snapshot.UpZ);
}

float UAudioEngine::GetBusVolume(AudioBus bus) const
{
  std::lock_guard<std::mutex> lock(VolumesMutex);
  if (!Volumes.Enabled)
  {
    return 0.f;
  }
  switch (bus)
  {
  case AudioBus::Music:
    return Volumes.Master * Volumes.Music;
  case AudioBus::Ambient:
    return Volumes.Master * Volumes.Ambient;
  case AudioBus::Sfx:
    return Volumes.Master * Volumes.Sfx;
  case AudioBus::Ui:
    return Volumes.Master * Volumes.Ui;
  }
  return 0.f;
}

const char *UAudioEngine::GetBackendName() const
{
  if (!State || !State->EngineReady)
  {
    return "none";
  }
  const ma_device *device = ma_engine_get_device(&State->Engine);
  if (!device || !device->pContext)
  {
    return "nodevice";
  }
  return ma_get_backend_name(device->pContext->backend);
}

void *UAudioEngine::GetRawEngine()
{
  return (State && State->EngineReady) ? &State->Engine : nullptr;
}

void *UAudioEngine::GetRawResourceManager()
{
  return (State && State->ResourceManagerReady) ? &State->ResourceManager
                                                : nullptr;
}

void *UAudioEngine::GetRawGroup(AudioBus bus)
{
  const int index = BusIndex(bus);
  if (!State || !State->GroupsReady[index])
  {
    return nullptr;
  }
  return &State->Groups[index];
}

} // namespace cutum
