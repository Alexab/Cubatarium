#pragma once
#include "Blocks/BlockDefinitionStorage.h"
#include "World/Chunks/ChunkInputStamp.h"
#include "World/Lighting/PersistedSectionLightTypes.h"
#include "World/Lighting/SectionLightContentHash.h"
#include <unordered_map>

namespace cutum
{
class UChunkRelightSnapshot;
class UBlockWorld;
enum class SectionLightRestoreResult;

// Only the actual snapshot solver or fully validated disk restore can create
// this evidence. It retains compact input contents and output hashes, never
// voxel snapshots/output volumes, and does not claim current input residency.
class RelightContentCertificate
{
public:
  static constexpr size_t MaxInputs = PersistedSectionLight::MaxDependencies;
  static constexpr size_t MaxOutputs = 4096;
  ~RelightContentCertificate() = default;
  const std::vector<PersistedSectionLight::Input> &Inputs() const
  {
    return Contents;
  }
  bool
  MatchesInputs(const std::vector<ChunkInputStamp> &stamps,
                const WorldVerticalProfile &profile, uint64_t configuration,
                const std::shared_ptr<const BlockDefinitionCatalog> &catalog,
                bool sky, bool block) const
  {
    if (Catalog != catalog || Configuration != configuration || Sky != sky ||
        Block != block || Profile.MinY != profile.MinY ||
        Profile.MaxY != profile.MaxY ||
        Profile.SeaLevelY != profile.SeaLevelY ||
        Profile.BedrockTopY != profile.BedrockTopY ||
        Profile.DatumY != profile.DatumY || stamps.size() != Stamps.size())
      return false;
    for (size_t n = 0; n < stamps.size(); ++n)
    {
      const auto &a = Stamps[n];
      const auto &b = stamps[n];
      if (a.coord != b.coord || a.incarnation != b.incarnation ||
          a.content != b.content || a.missingEvidence != b.missingEvidence ||
          a.readsContent != b.readsContent || a.readsLight != b.readsLight)
        return false;
    }
    return !Contents.empty() && Contents.size() == stamps.size();
  }
  bool MatchesOutput(glm::ivec3 coord, const SectionLightVolume &volume,
                     int domain) const
  {
    const auto found = Outputs.find(coord);
    return domain >= 0 && domain < 2 && (domain == 0 ? Sky : Block) &&
           found != Outputs.end() &&
           found->second[domain] == SectionLightDomainHash(volume, domain);
  }

private:
  RelightContentCertificate() = default;
  friend class UChunkRelightSnapshot;
  friend SectionLightRestoreResult
  RestoreSectionLight(UBlockWorld &, const PersistedSectionLight &,
                      const std::shared_ptr<const BlockDefinitionCatalog> &);
  std::vector<ChunkInputStamp> Stamps;
  std::vector<PersistedSectionLight::Input> Contents;
  WorldVerticalProfile Profile;
  uint64_t Configuration{0};
  std::shared_ptr<const BlockDefinitionCatalog> Catalog;
  bool Sky{false}, Block{false};
  std::unordered_map<glm::ivec3, std::array<uint64_t, 2>, IVec3Hash> Outputs;
};
} // namespace cutum
