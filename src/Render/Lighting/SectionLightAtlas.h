#pragma once

#include "Render/GlIncludes.h"
#include "World/Lighting/SectionLightVolume.h"
#include <cstddef>
#include <cstdint>
#include <glm/glm.hpp>
#include <limits>
#include <memory>
#include <optional>
#include <unordered_map>
#include <vector>

namespace cutum
{
/// Immutable GPU storage, not calculation authority. The publication owner
/// must validate physical provenance and geometry compatibility before binding
/// a page. Uploading zero bytes never supplies that proof.
class SectionLightAtlas
{
public:
  struct Layout
  {
    int tiles_per_axis{0};
    int texture_width{0};
    uint32_t capacity{0};
    size_t bytes{0};
    static Layout Choose(int max_texture_width, size_t byte_budget,
                         uint32_t page_limit);
  };
  struct Owner
  {
    uint64_t world_epoch{0};
    uint64_t field_incarnation{0};
    uint64_t field_revision{0};
    bool operator==(const Owner &other) const
    {
      return world_epoch == other.world_epoch &&
             field_incarnation == other.field_incarnation &&
             field_revision == other.field_revision;
    }
  };
  struct Page
  {
    uint64_t atlas_incarnation{0};
    uint32_t slot{0};
    uint32_t generation{0};
    bool valid{false};
    bool operator==(const Page &other) const
    {
      return atlas_incarnation == other.atlas_incarnation &&
             slot == other.slot && generation == other.generation &&
             valid == other.valid;
    }
  };
  struct Statistics
  {
    uint32_t live{0}, retired{0}, quarantined{0};
    uint64_t uploads{0}, uploaded_bytes{0};
    uint64_t ordered_uploads{0};
    uint32_t ordered_pages{0};
  };

  SectionLightAtlas() = default;
  ~SectionLightAtlas();
  SectionLightAtlas(const SectionLightAtlas &) = delete;
  SectionLightAtlas &operator=(const SectionLightAtlas &) = delete;

  /// All operations run on the owning render context/thread. Reset must run
  /// before that context is destroyed. No synchronous fence wait is used.
  bool Initialize(size_t byte_budget = 32u * 1024u * 1024u,
                  uint32_t page_limit = 4096,
                  bool reserve_ordered_page = false);
  void Reset();
  std::optional<Page> Acquire(Owner owner,
                              std::shared_ptr<const SectionLightVolume> volume);
  /// One reserved page inside the configured budget. Submit its draw before
  /// the next call: uploads/draws share the owning GL command stream. Reuse
  /// invalidates previous CPU tickets; this is not a long-lived resident page.
  std::optional<Page>
  StageForOrderedDraw(Owner owner,
                      std::shared_ptr<const SectionLightVolume> volume);
  /// One reference per successful Acquire. The final Release must follow the
  /// last GL submission which consumes the page, including all render passes.
  bool Release(Page page);
  void CollectRetired();
  bool IsLive(Page page) const;
  std::optional<glm::ivec3> Origin(Page page) const;
  bool Bind(unsigned texture_unit) const;
  const Layout &GetLayout() const { return AtlasLayout; }
  Statistics GetStatistics() const;
  void InspectRenderImages(class RenderImageCensus &out) const;

private:
  enum class State : uint8_t
  {
    Free,
    Live,
    Retired,
    Quarantined,
    Ordered
  };
  struct Slot
  {
    State state{State::Free};
    Owner owner;
    uint32_t generation{0}, references{0};
    GLsync fence{nullptr};
    std::shared_ptr<const SectionLightVolume> volume;
  };
  struct OwnerHash
  {
    size_t operator()(const Owner &owner) const;
  };
  glm::ivec3 SlotOrigin(uint32_t slot) const;
  bool Upload(uint32_t slot, const SectionLightVolume &volume);
  GLuint Texture{0};
  GLint TextureUnits{0};
  Layout AtlasLayout;
  uint64_t Incarnation{0};
  uint64_t Uploads{0}, UploadedBytes{0};
  uint64_t OrderedUploads{0};
  uint32_t OrderedIndex{std::numeric_limits<uint32_t>::max()};
  std::vector<Slot> Slots;
  std::vector<uint32_t> Free;
  std::unordered_map<Owner, uint32_t, OwnerHash> Owners;
};
} // namespace cutum
