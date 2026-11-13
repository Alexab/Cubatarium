# Audio system design research — 2026-11-12

Scope: greenfield sound support for Cubatarium (engine had **zero** audio
code; the only pre-existing seam was `EffectNodeSpec::SoundId`, unused).

## Requirements (user-confirmed)

1. Playback never affects FPS / never blocks gameplay — no decode, device
   waits or locks on the game thread.
2. Action sounds are **synchronous with activity** (footstep at the
   footfall, hit at the damage application) with ~1–10 ms event→sound
   latency; "asynchronous" refers to the engine (non-blocking posting +
   dedicated threads), not to sound behavior.
3. Repeated sounds vary ±10% pitch/gain + variant selection (anti-repetition
   standard).
4. Biome ambience: continuous loops crossfading on biome change + rare
   random stingers on timers (background, not activity-driven).
5. Footsteps for **all creatures and the player**, matching both the
   creature (archetype flavor) and the surface material under the feet.
6. Music: user-selected Ragnarok Online OST tracks, local only, never
   committed (`/bin/audio/` gitignored).
7. Audio settings (enable + 5 bus sliders) in `config.json` and the settings
   GUI.

## Backend choice

**miniaudio v0.11.26** vendored single header (`src/ThirdParty/miniaudio.h`,
public domain / MIT-0), same pattern as stb_image. Rationale over OpenAL
Soft / SoLoud / SDL_mixer:

- zero new vcpkg dependencies (static CRT `/MT` build stays intact),
- built-in WAV / **MP3** / FLAC decoders (khinsider MP3s play without
  transcoding; note: Vorbis needs stb_vorbis vendoring — not built-in),
- `ma_engine` high-level API with 3D spatialization, listener, sound groups
  (buses) and a resource manager on its own thread,
- WASAPI on Windows, AAudio on Android (the android target compiles the same
  sources),
- runtime-loads `ole32.dll` — no link-time Windows dependencies.

Verified API notes (0.11.26): `ma_engine_config` has **no maxVoices**
(concurrency is capped app-side by per-set voice pools);
`ma_resource_manager_uninit` takes 1 argument; listener = `set_position` +
`set_direction` + `set_world_up`; `ma_get_backend_name(ma_device->pContext->backend)`.

## Architecture

```
Game thread (per frame ~0 audio work)              Audio thread (director)
---------------------------------------            --------------------------------
gameplay code → AudioApi::Post(POD event)  →→→→   ring queue (condvar wake) →
  (memcpy + notify, no alloc, no I/O)               immediate playback (~1–10 ms)
listener pose → tiny mutex copy (per frame)         ±10% pitch/gain jitter
ambience snapshot (4 Hz, throttled)                 voice cooldowns (min_interval_sec
                                                    × creature interval_mul)
                                                    music playlist crossfades (3 s)
                                                    biome/weather loop crossfades
                                                    random stingers 15–180 s
                                                    ↓
                                            UAudioEngine (owns ma_engine)
                                            buses: Master→{Music,Ambient,Sfx,Ui}
```

miniaudio additionally runs its own device thread + resource-manager thread.
Focus loss mutes the whole mixer (`SetFocusMuted`, config untouched).

## Files

- `src/ThirdParty/miniaudio.h/.cpp` — vendored backend.
- `src/Audio/AudioEvent.h` — POD events (kind/pos/set/material/archetype),
  `AudioListenerSnapshot`, `AudioAmbienceSnapshot` (fixed buffers, no
  allocation).
- `src/Audio/AudioEventQueue.h/.cpp` — bounded ring, drop-oldest overflow,
  condvar wake.
- `src/Audio/AudioApi.h/.cpp` — the only surface gameplay code touches.
- `src/Audio/AudioEngine.h/.cpp` — ma_engine + 4 bus groups; pimpl keeps the
  97k-line vendor header out of gameplay builds.
- `src/Audio/SoundManifest.h/.cpp` + `content/audio/sounds.json` — set
  definitions (variants, gain, distance, min_interval, loop flag).
- `src/Audio/SoundBank.h/.cpp` — per-set pools of 4 preloaded voices,
  round-robin with anti-repeat, machine-gun cooldowns. Missing files warn
  once and silence (PlaceholderTextureCache degradation pattern).
- `src/Audio/MaterialSoundMap.h/.cpp` + `content/audio/material_sounds.json`
  — block name/dig-group → surface material; biome → ambience sets;
  locomotion archetype → footstep flavor (pitch/interval multipliers).
  Shared read-only instance (`GetAudioMaterialMap`).
- `src/Audio/MusicManifest.h/.cpp` + `content/audio/music_manifest.json` —
  context → playlist, longest-contained-key matching, `overworld` fallback.
- `src/Audio/AudioDirector.h/.cpp` — audio thread: event consumption,
  settings, music/ambience housekeeping, device-retry backoff (5 s),
  telemetry counters.

## Gameplay hooks (all POD posts, no direct playback)

| Sound | Site |
|---|---|
| Footsteps/landing/splash (all creatures + player) | `WorldViewBinding.cpp` `EmitCreatureFootstepAudio` — half-cycle crossings of the walk phase (`ConsumeFootstepAudio`, `CreatureLocomotionFacts.cpp`) + grounded transitions; material via `UWorld::ResolveAudioMaterialAt` |
| Dig hit / block broken | `InfluenceApplier.cpp` (dig progress + complete; material resolved before the block disappears) |
| Block placed | `BlockPlacementService.cpp` (`PostPlaceAudio`) |
| Melee swing / hit, bow shot, eat | `InfluenceApplier.cpp` + `Application::NotifyFpSwing` |
| Pickup | `WorldItemSystem::Tick` |
| Hurt / death | `CreatureVitalsSystem.cpp` |
| Equip / unequip | `CreatureInventory.cpp` |
| Inventory drag/drop | `GameSession.cpp` |
| Menu screens | `ScreenNavigator.cpp` |

## Settings

`config.json` `"audio"` section (enabled/master/music/ambient/sfx/ui,
0..1 clamped). Settings GUI gained the **Audio** tab (4th tab in
`USettingsScreen`): enable checkbox + five 0–100% sliders (`UGuiSlider`),
live-apply on commit via `IUGuiMenuHost::ApplyLiveAudioSettings` →
`UCore::ApplyAudioSettingsLive`, persistence through the standard
snapshot → `ApplyAppSettings` → `SaveConfigFile` pipeline.

## Surface material mapping

Exact block name first (longest list wins), then Luanti dig-group fallback
(`cracky`→stone, `crumbly`→soft, `choppy`→wood, `snappy`→leaf) — the
semantics already exist in `UBlockDefinitionStorage`. Creature flavor
multiplies pitch (quadruped 0.85) and cooldown interval (aquatic 1.6...) so
the same surface sounds different per body; unknown surfaces fall back to
the creature's archetype set.

## Acceptance / verification

- Unit tests: `audio_event_queue_test` (post/drain/overflow/wake),
  `audio_settings_json_test` (roundtrip+clamp), `material_sound_map_test`
  (resolution + fallbacks), `footstep_phase_wrap_test` (footfall cadence,
  landing, idle/air), `music_manifest_test` (matching + fallback).
- Release build + `tools/flight_sim_run.py` gate (no FPS regression; audio
  posts are memcpy-scale and the director is off the frame path).
- No audio files installed → game runs silent with one-time warnings.
- Focus loss mutes; sliders apply live and persist across restarts.

## Known limitations / follow-ups

- Per-species voices (creature.json `sounds` section) are not wired yet —
  generic hurt/death sets are used.
- Music slots fade at the 10 Hz housekeeping cadence (30 linear steps over
  3 s) — smooth enough, but a frame-rate-independent ramp could be nicer.
- Ambient torch crackle set exists in the manifest but no torch-proximity
  scheduler is wired yet.
- `EffectSpec::SoundId` remains unused; effect-driven sounds can reuse the
  same post pipeline later.
