#pragma once

#include <cstdint>

namespace cutum
{

/// Mixer buses; each maps to one ma_sound_group with its own volume slider.
enum class AudioBus : uint8_t
{
  Music,
  Ambient,
  Sfx,
  Ui,
};

/// Gameplay-driven sound events. POD so posting never allocates.
/// Gameplay code only posts these; all playback happens on the audio thread
/// (see AudioDirector), which keeps audio off the frame budget entirely.
enum class AudioEventKind : uint8_t
{
  Footstep,
  Land,
  DigHit,
  DigComplete,
  Place,
  MeleeSwing,
  MeleeHit,
  RangedShot,
  Eat,
  Pickup,
  Equip,
  Unequip,
  UiClick,
  UiOpen,
  UiClose,
  CreatureHurt,
  CreatureDeath,
  CreatureVoice,
  Splash,
};

struct UAudioEvent
{
  AudioEventKind Kind{AudioEventKind::UiClick};
  /// World-space position of the sound source; UI events may leave this at
  /// the listener position (director falls back when Strength==0).
  float X{0.f};
  float Y{0.f};
  float Z{0.f};
  /// Sound set id inside the SoundBank (stage 2); 0 = unresolved (dropped).
  uint32_t SetId{0};
  /// Surface material id from MaterialSoundMap (footsteps/dig).
  uint32_t MaterialId{0};
  /// LocomotionArchetype value of the emitting creature (flavor pitch/interval
  /// on the director side); 255 = unknown.
  uint8_t Archetype{255};
  /// Emitting creature id (0 = player/world).
  uint32_t EntityId{0};
  /// 0 = walk, 1 = run, 2 = crouch (footsteps); free-form for other kinds.
  uint8_t RunState{0};
  /// Relative loudness multiplier (hit strength, splash depth...).
  float Strength{1.f};
};

/// Listener pose posted once per frame from the game thread. Copyable POD
/// guarded by a mutex inside AudioApi (tiny, uncontended).
struct AudioListenerSnapshot
{
  float X{0.f};
  float Y{0.f};
  float Z{0.f};
  float FwdX{0.f};
  float FwdY{-1.f};
  float FwdZ{0.f};
  float UpX{0.f};
  float UpY{1.f};
  float UpZ{0.f};
  /// False until the camera posts its first snapshot; director skips
  /// spatialization updates until then.
  bool Valid{false};
};

/// 1..2 walking, 3 running, 4 crouching — mirrors LocomotionState ordering
/// without pulling locomotion headers into Audio.
enum class AudioRunState : uint8_t
{
  Walk = 0,
  Run = 1,
  Crouch = 2,
};

/// World context posted (cheap POD copy) for ambience/music scheduling.
/// Fixed buffers keep this allocation-free; truncated ids are acceptable —
/// prefix containment matching still hits the right playlist family.
struct AudioAmbienceSnapshot
{
  char BiomeId[48];
  char WeatherName[24];
  bool InGame{false};
  bool IsNight{false};
  bool Valid{false};
};

} // namespace cutum
