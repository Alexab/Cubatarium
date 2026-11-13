#pragma once

#include "App/Settings/AudioSettings.h"
#include "Audio/AudioEvent.h"

#include <mutex>

namespace cutum
{

/// Owns the miniaudio engine: resource manager, device and the bus hierarchy
/// Master → {Music, Ambient, Sfx, Ui}. Lifecycle calls (Init/Shutdown) must
/// come from the audio director thread; volume/listener setters are
/// thread-safe. All miniaudio types stay inside the .cpp (pimpl) so gameplay
/// builds never pull the 97k-line vendor header.
class UAudioEngine
{
public:
  UAudioEngine() = default;
  ~UAudioEngine();

  UAudioEngine(const UAudioEngine &) = delete;
  UAudioEngine &operator=(const UAudioEngine &) = delete;

  /// Creates the device + bus groups. Returns false when no audio device is
  /// available (headless runs) — every other call then no-ops.
  bool Init(const AudioSettings &settings);
  void Shutdown();

  bool IsReady() const { return Ready; }

  /// Applies per-bus volumes; thread-safe, called on settings change.
  void ApplyVolumes(const AudioSettings &settings);

  /// Updates the 3D listener pose; called from the director thread.
  void SetListener(const AudioListenerSnapshot &snapshot);

  /// Bus gain including the master multiplier and mute state.
  float GetBusVolume(AudioBus bus) const;

  /// Human-readable backend name for the startup log ("WASAPI", ...).
  const char *GetBackendName() const;

  /// Raw miniaudio handles for the SoundBank (audio-thread only). Returns
  /// nullptr until Init succeeds. Erased to void* so this header stays
  /// free of the miniaudio include.
  void *GetRawEngine();
  void *GetRawResourceManager();
  void *GetRawGroup(AudioBus bus);

private:
  /// Opaque miniaudio state (resource manager, engine, bus groups).
  struct MiniaudioState;
  MiniaudioState *State{nullptr};
  bool Ready{false};

  mutable std::mutex VolumesMutex;
  AudioSettings Volumes;
};

} // namespace cutum
