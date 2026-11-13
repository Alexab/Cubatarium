#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace cutum
{

class UAudioNameRegistry;

/// One surface material: which blocks map to it and which sound sets play.
struct SurfaceMaterialDef
{
  std::string Name;                   // "grass"
  std::vector<std::string> Blocks;    // exact block names ("grass", "wood"...)
  std::vector<std::string> DigGroups; // Luanti dig group fallback ("crumbly")
  std::string FootstepSet;            // "footstep.grass"
  std::string DigSet;                 // progress hit set ("dig.grass")
  std::string DugSet;                 // completed break set ("dug.grass")
};

/// Default footstep sets per locomotion archetype (covers all species
/// without touching creature.json; per-species overrides come from stage 4).
/// Flavor fields reshape the surface sound for heavier/lighter bodies.
struct CreatureSoundDefaultDef
{
  std::string Archetype; // "terrestrial_biped", "terrestrial_quadruped"...
  std::string FootstepSet; // used when the surface material is unknown
  float PitchMul{1.f};     // hooves 0.85, paws 1.1, ...
  float IntervalMul{1.f};  // cooldown scale (bigger = slower repeats)
};

/// Continuous loop + random one-shot stingers for a biome family.
struct BiomeAmbienceDef
{
  std::string Biome; // biome id ("forest") — longest-prefix match at resolve
  std::string LoopSet;
  std::vector<std::string> DayStingers;
  std::vector<std::string> NightStingers;
};

struct WeatherAmbienceDef
{
  std::string Weather; // "clear", "rain"...
  std::string LoopSet; // overlay loop on top of biome ambience
};

/// Data-driven mapping loaded from content/audio/material_sounds.json.
/// Game threads call ResolveMaterial (hash-free linear scans over a small
/// list are fine at gameplay rates); the director resolves set names.
class UMaterialSoundMap
{
public:
  bool LoadFromFile(const std::string &path);
  bool Load(const void *jsonRoot);

  /// Exact block name first, then dig group, then unknown (0).
  std::uint32_t ResolveMaterial(const std::string &blockName,
                                const std::string &digGroupsCsv) const;

  const SurfaceMaterialDef *GetMaterial(std::uint32_t materialId) const;
  std::uint32_t GetMaterialCount() const
  {
    return static_cast<std::uint32_t>(Materials.size());
  }

  /// Longest biome-id prefix wins ("dense_forest" → "forest"); "" = fallback.
  const BiomeAmbienceDef *ResolveBiome(const std::string &biomeId) const;
  const BiomeAmbienceDef &GetFallbackBiome() const { return FallbackBiome; }

  const CreatureSoundDefaultDef *
  FindCreatureDefault(const std::string &archetype) const;

  const WeatherAmbienceDef *
  FindWeather(const std::string &weatherName) const;

  const std::vector<SurfaceMaterialDef> &GetMaterials() const
  {
    return Materials;
  }
  const std::vector<CreatureSoundDefaultDef> &GetCreatureDefaults() const
  {
    return CreatureDefaults;
  }
  const std::vector<BiomeAmbienceDef> &GetBiomes() const { return Biomes; }
  const std::vector<WeatherAmbienceDef> &GetWeathers() const
  {
    return Weathers;
  }

private:
  std::vector<SurfaceMaterialDef> Materials;
  std::vector<CreatureSoundDefaultDef> CreatureDefaults;
  std::vector<BiomeAmbienceDef> Biomes;
  std::vector<WeatherAmbienceDef> Weathers;
  BiomeAmbienceDef FallbackBiome;
};

/// Process-wide read-only mapping shared by gameplay post sites and the
/// audio director (loaded once; concurrent const reads are safe).
/// Returns nullptr until LoadAudioMaterialMap succeeds.
const UMaterialSoundMap *GetAudioMaterialMap();
bool LoadAudioMaterialMap(const std::string &path);

} // namespace cutum
