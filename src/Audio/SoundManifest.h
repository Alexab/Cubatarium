#pragma once

#include "Audio/AudioEvent.h"

#include <cstdint>
#include <string>
#include <vector>

namespace cutum
{

struct SoundSetDef
{
  std::string Name;                // "footstep.grass" — referenced by mappings.
  AudioBus Bus{AudioBus::Sfx};
  float BaseGain{1.f};
  /// Reference distance for 3D attenuation (0 = global/UI sound).
  float BaseDistance{0.f};
  /// Minimum seconds between starts of this set (machine-gun guard).
  float MinIntervalSec{0.f};
  /// Continuous ambience loop — the director streams these separately; the
  /// bank skips pool creation for them.
  bool Loop{false};
  /// Variant file paths relative to the sounds root
  /// (e.g. "footstep/grass/variant_1.wav").
  std::vector<std::string> Files;
};

/// Parse-only manifest for content/audio/sounds.json. Device-free so unit
/// tests cover it without an audio backend.
class USoundManifest
{
public:
  bool LoadFromFile(const std::string &path);
  bool Load(const void *jsonRoot);

  const SoundSetDef *FindByName(const std::string &name) const;
  std::size_t GetSetCount() const { return Sets.size(); }
  const std::vector<SoundSetDef> &GetSets() const { return Sets; }

private:
  std::vector<SoundSetDef> Sets;
};

/// Interned string→id registry shared by material/biome mappings. Ids are
/// stable within one process (load order); 0 is reserved "unknown".
class UAudioNameRegistry
{
public:
  static constexpr std::uint32_t kUnknownId = 0;

  std::uint32_t Intern(const std::string &name);
  const std::string *GetName(std::uint32_t id) const;

private:
  std::vector<std::string> Names{"?"};
};

} // namespace cutum
