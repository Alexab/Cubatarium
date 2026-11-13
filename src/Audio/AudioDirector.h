#pragma once

#include "App/Settings/AudioSettings.h"
#include "Audio/AudioEngine.h"
#include "Audio/AudioEventQueue.h"
#include "Audio/MaterialSoundMap.h"
#include "Audio/MusicManifest.h"
#include "Audio/SoundBank.h"
#include "Audio/SoundManifest.h"

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <mutex>
#include <random>
#include <thread>
#include <vector>

namespace cutum
{

/// Owns the audio thread: consumes gameplay events (immediate playback so
/// action sounds stay in sync with activity), applies settings and — in
/// later stages — schedules biome ambience and music playlists. The game
/// thread only talks to this class via UAudioApi posts and cheap setters.
class UAudioDirector
{
public:
  UAudioDirector() = default;
  ~UAudioDirector();

  UAudioDirector(const UAudioDirector &) = delete;
  UAudioDirector &operator=(const UAudioDirector &) = delete;

  /// Binds UAudioApi and spawns the worker thread. Safe to call with audio
  /// disabled (thread idles until enabled). Paths:
  ///  - @p contentAudioDir: data dir with sounds.json / material_sounds.json
  ///  - @p soundsRoot: runtime audio files root (bin/audio/sounds)
  ///  - @p musicRoot: user music files root (bin/audio/music)
  void Start(const AudioSettings &settings,
             const std::filesystem::path &contentAudioDir,
             const std::filesystem::path &soundsRoot,
             const std::filesystem::path &musicRoot);

  /// Signals the worker to stop, joins it and unbinds UAudioApi.
  void Stop();

  /// Thread-safe settings update; applied by the worker within ~100 ms.
  void ApplySettings(const AudioSettings &settings);

  /// Window focus loss mutes everything without touching the config.
  void SetFocusMuted(bool muted);

  UAudioEventQueue &GetQueue() { return Queue; }

  /// Opaque streaming music/loop state (defined in the .cpp, audio thread).
  struct HousekeepingState;

  /// Telemetry counters for perf overlays / QA.
  std::uint64_t GetEventsHandled() const { return EventsHandled.load(); }
  std::uint64_t GetQueueOverflow() const { return Queue.DiscardedOverflow(); }

private:
  void Run();
  AudioSettings ReadSettingsSnapshot() const;
  /// Engine lifecycle on the audio thread with device-retry backoff.
  void EnsureEngine(const AudioSettings &settings);
  void LoadContent();
  void HandleEvent(const UAudioEvent &event, const AudioSettings &settings);
  /// 10 Hz housekeeping: music playlists, biome/weather loops, stingers.
  void TickHousekeeping(float dt);
  /// Music bus only. An empty context fades music out and leaves ambience.
  void TickMusic(const AudioAmbienceSnapshot &amb, float fadeStep);
  void ResetHousekeeping();

  std::uint32_t ResolveEventSet(const UAudioEvent &event) const;
  /// Variation so repeated samples don't machine-gun (±10%).
  float SampleJitter();

  UAudioEventQueue Queue;
  UAudioEngine Engine;
  USoundManifest Manifest;
  UMusicManifest MusicManifest;
  USoundBank Bank;
  std::thread Worker;
  std::atomic<bool> Running{false};

  mutable std::mutex SettingsMutex;
  AudioSettings SettingsSnapshot;
  std::atomic<bool> FocusMuted{false};

  /// Pushes SettingsSnapshot (with focus mute folded in) to the engine.
  void ApplyEffectiveVolumes();

  std::filesystem::path ContentAudioDir;
  std::filesystem::path SoundsRoot;
  std::filesystem::path MusicRoot;
  bool ContentLoaded{false};

  /// Streaming music/loop voices + schedulers (audio thread only).
  HousekeepingState *Hk{nullptr};

  /// Audio-thread clock (seconds since director start) for cooldowns.
  float NowSec{0.f};
  std::mt19937 Random{12345};
  std::uniform_real_distribution<float> Jitter{-0.1f, 0.1f};

  std::atomic<std::uint64_t> EventsHandled{0};
  float DeviceRetryCooldownSec{0.f};
};

} // namespace cutum
