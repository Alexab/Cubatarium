#pragma once

#include "Audio/AudioEvent.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

namespace cutum
{

class UAudioEngine;
class USoundManifest;

/// Runtime playback bank for one-shot sets (footsteps, hits, UI...).
/// Owned by the director thread only. Pools of preloaded voices per set,
/// round-robin selection with anti-repeat; pitch/gain variation is applied
/// by the director before Play(). Missing files warn once and stay silent —
/// the game keeps running without assets (PlaceholderTextureCache pattern).
class USoundBank
{
public:
  USoundBank() = default;
  ~USoundBank();

  USoundBank(const USoundBank &) = delete;
  USoundBank &operator=(const USoundBank &) = delete;

  /// Loads the manifest and creates voice pools on the engine's resource
  /// manager. @p soundsRoot is the absolute directory holding set folders.
  /// Returns the number of sets that resolved at least one existing file.
  std::size_t Init(UAudioEngine *engine, const USoundManifest &manifest,
                   const std::filesystem::path &soundsRoot);
  void Shutdown();

  bool IsReady() const { return Ready; }

  /// Resolves a set name to a bank id (0 = unknown); stable after Init.
  std::uint32_t ResolveId(const std::string &name) const;
  const char *GetName(std::uint32_t bankId) const;

  /// Starts one voice of the set at a world position. @p pitch and
  /// @p gainMul already include their random variation.
  void Play(std::uint32_t bankId, float x, float y, float z, float pitch,
            float gainMul, float strength);

  /// Machine-gun protection for rapid-fire sets (dig hits, footsteps).
  /// @p intervalMul scales the set's min interval (heavier creatures).
  bool IsOnCooldown(std::uint32_t bankId, float nowSec,
                    float intervalMul = 1.f) const;
  void MarkPlayed(std::uint32_t bankId, float nowSec);

  std::size_t GetSetCount() const { return Sets.size(); }

private:
  /// Header-safe metadata; all miniaudio objects live in the pimpl state.
  struct BankSet
  {
    std::string Name;
    AudioBus Bus{AudioBus::Sfx};
    float BaseGain{1.f};
    float BaseDistance{0.f};
    /// Index/count into the flat per-set voice pool.
    std::uint32_t VoiceBase{0};
    std::uint32_t VoiceCount{0};
    /// Index/count into the flat decoded variant buffers.
    std::uint32_t VariantBase{0};
    std::uint32_t VariantCount{0};
    std::size_t NextVoice{0};
    float LastPlaySec{-1000.f};
    float MinIntervalSec{0.f};
  };

  BankSet *GetSet(std::uint32_t bankId);
  const BankSet *GetSet(std::uint32_t bankId) const;

  struct State;
  State *State_{nullptr};
  std::vector<BankSet> Sets;
  std::unordered_map<std::string, std::uint32_t> IdsByName;
  bool Ready{false};
};

} // namespace cutum
