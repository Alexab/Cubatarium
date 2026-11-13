#pragma once

#include "Audio/AudioEvent.h"

namespace cutum
{

class UAudioEventQueue;

/// Thin non-blocking facade between gameplay code and the audio director.
///
/// Gameplay code must never call the engine directly: Post() is a memcpy +
/// condvar notify (~microseconds), UpdateListener() is a tiny mutex-guarded
/// copy. Both are safe to call every frame from anywhere in the game loop.
/// Before SetQueue() every call is a no-op (headless runs, early startup).
class UAudioApi
{
public:
  /// Installs the event queue + engine handle used by the director.
  /// Called once on director start; cleared on stop. Not thread-safe with
  /// concurrent Post, which is fine (director lifecycle is app-scope).
  static void Bind(class UAudioEventQueue *queue);

  /// Posts a gameplay sound event. No-op when unbound or queue is null.
  static void Post(const UAudioEvent &event);

  /// Per-frame listener pose from the active camera. Safe from any thread.
  static void UpdateListener(const AudioListenerSnapshot &snapshot);

  /// Latest listener pose (audio thread).
  static AudioListenerSnapshot ReadListener();

  /// World context (biome/weather/night) for ambience + music selection.
  static void UpdateAmbience(const AudioAmbienceSnapshot &snapshot);
  static AudioAmbienceSnapshot ReadAmbience();

  /// True while the director consumes events (used by QA smoke sinks).
  static bool IsBound();

private:
  static UAudioEventQueue *Queue;
};

} // namespace cutum
