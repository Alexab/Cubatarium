#include "WorldGen/Sampling/BiomeRegistry.h"
#include "WorldGen/Core/WorldGenPack.h"

namespace cutum
{

namespace
{

BiomeId ParseBiomeId(const std::string &name)
{
  if (name == "forest")
  {
    return BiomeId::Forest;
  }
  if (name == "desert")
  {
    return BiomeId::Desert;
  }
  if (name == "hills")
  {
    return BiomeId::Hills;
  }
  if (name == "tundra")
  {
    return BiomeId::Tundra;
  }
  if (name == "savanna")
  {
    return BiomeId::Savanna;
  }
  if (name == "foothills")
  {
    return BiomeId::Foothills;
  }
  if (name == "scrubland")
  {
    return BiomeId::Scrubland;
  }
  if (name == "cold_steppe")
  {
    return BiomeId::ColdSteppe;
  }
  return BiomeId::Plains;
}

const char *BiomeIdToStringImpl(BiomeId biome)
{
  switch (biome)
  {
  case BiomeId::Forest:
    return "forest";
  case BiomeId::Desert:
    return "desert";
  case BiomeId::Hills:
    return "hills";
  case BiomeId::Tundra:
    return "tundra";
  case BiomeId::Savanna:
    return "savanna";
  case BiomeId::Foothills:
    return "foothills";
  case BiomeId::Scrubland:
    return "scrubland";
  case BiomeId::ColdSteppe:
    return "cold_steppe";
  case BiomeId::Plains:
  default:
    return "plains";
  }
}

UBiomeRegistry gActiveBiomeRegistry;

} // namespace

void UBiomeRegistry::LoadFromPack(const WorldGenPack &pack)
{
  StringToId.clear();
  IdToString.clear();
  MapColors.clear();

  const auto register_biome = [&](const std::string &name)
  {
    const BiomeId id = ParseBiomeId(name);
    StringToId[name] = id;
    IdToString[id] = name;
  };

  for (const auto &entry : pack.Biomes)
  {
    register_biome(entry.first);
  }

  register_biome("plains");
  register_biome("forest");
  register_biome("desert");
  register_biome("hills");
  register_biome("tundra");
  register_biome("savanna");
  register_biome("foothills");
  register_biome("scrubland");
  register_biome("cold_steppe");
}

BiomeId UBiomeRegistry::IdFromString(const std::string &name) const
{
  const auto it = StringToId.find(name);
  if (it != StringToId.end())
  {
    return it->second;
  }
  return ParseBiomeId(name);
}

const char *UBiomeRegistry::StringFromId(BiomeId id) const
{
  const auto it = IdToString.find(id);
  if (it != IdToString.end())
  {
    return it->second.c_str();
  }
  return BiomeIdToStringImpl(id);
}

std::optional<glm::ivec3> UBiomeRegistry::MapColorFor(BiomeId id) const
{
  const auto it = MapColors.find(id);
  if (it != MapColors.end())
  {
    return it->second;
  }
  return std::nullopt;
}

UBiomeRegistry &GetActiveBiomeRegistry()
{
  return gActiveBiomeRegistry;
}

BiomeId BiomeIdFromString(const std::string &name)
{
  return GetActiveBiomeRegistry().IdFromString(name);
}

const char *BiomeIdToString(BiomeId biome)
{
  return GetActiveBiomeRegistry().StringFromId(biome);
}

} // namespace cutum
