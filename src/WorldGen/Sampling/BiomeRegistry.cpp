#include "WorldGen/Sampling/BiomeRegistry.h"
#include "WorldGen/Core/WorldGenPack.h"

namespace cutum
{

namespace
{

BiomeId ParseBiomeId(const std::string &name)
{
  if (name == "plains")
  {
    return BiomeId::Plains;
  }
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
  if (name == "ocean")
  {
    return BiomeId::Ocean;
  }
  if (name == "beach")
  {
    return BiomeId::Beach;
  }
  if (name == "mountains")
  {
    return BiomeId::Mountains;
  }
  if (name == "snowy_slopes")
  {
    return BiomeId::SnowySlopes;
  }
  if (name == "jagged_peaks")
  {
    return BiomeId::JaggedPeaks;
  }
  if (name == "lush_caves")
  {
    return BiomeId::LushCaves;
  }
  if (name == "dripstone_caves")
  {
    return BiomeId::DripstoneCaves;
  }
  if (name == "deep_dark")
  {
    return BiomeId::DeepDark;
  }
  return BiomeId::Invalid;
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
  case BiomeId::Ocean:
    return "ocean";
  case BiomeId::Beach:
    return "beach";
  case BiomeId::Mountains:
    return "mountains";
  case BiomeId::SnowySlopes:
    return "snowy_slopes";
  case BiomeId::JaggedPeaks:
    return "jagged_peaks";
  case BiomeId::LushCaves:
    return "lush_caves";
  case BiomeId::DripstoneCaves:
    return "dripstone_caves";
  case BiomeId::DeepDark:
    return "deep_dark";
  case BiomeId::Plains:
    return "plains";
  case BiomeId::Invalid:
  default:
    return "invalid";
  }
}

UBiomeRegistry gActiveBiomeRegistry;

} // namespace

void UBiomeRegistry::LoadFromPack(const WorldGenPack &pack) { (void)pack; }

BiomeId UBiomeRegistry::IdFromString(const std::string &name) const
{
  return ParseBiomeId(name);
}

const char *UBiomeRegistry::StringFromId(BiomeId id) const
{
  return BiomeIdToStringImpl(id);
}

std::optional<glm::ivec3> UBiomeRegistry::MapColorFor(BiomeId id) const
{
  (void)id;
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
