#pragma once

#include "Render/Lighting/SectionLightAtlas.h"
#include <array>

namespace cutum
{
/// Render-context storage only. Callers must validate physical provenance and
/// geometry compatibility before Create/Replace; these operations cannot make
/// an arbitrary volume into a calculated field. Owns one atlas reference per
/// live binding. A stable handle allows light changes without vertex uploads.
class SectionLightBindingTable
{
public:
  struct Handle
  {
    uint32_t index{0}, generation{0};
    uint64_t table_incarnation{0};
    bool valid{false};
    std::array<uint32_t, 4> ShaderWords() const;
    bool operator==(const Handle &other) const;
  };

  SectionLightBindingTable() = default;
  ~SectionLightBindingTable();
  SectionLightBindingTable(const SectionLightBindingTable &) = delete;
  SectionLightBindingTable &
  operator=(const SectionLightBindingTable &) = delete;

  bool Initialize(uint32_t binding_limit = 4096,
                  size_t atlas_bytes = 32u * 1024u * 1024u,
                  uint32_t page_limit = 4096,
                  bool reserve_ordered_draw = false);
  /// Reset and destruction require the owning GL context, after last draw.
  void Reset();
  std::optional<Handle> Create(SectionLightAtlas::Owner owner,
                               std::shared_ptr<const SectionLightVolume> data,
                               glm::ivec3 section_origin);
  /// Acquire/upload the successor before replacing a live entry. Failure keeps
  /// its previous field and handle. Release old pages after the ordered update;
  /// atlas fences protect any earlier submissions still sampling those pages.
  bool Replace(Handle handle, SectionLightAtlas::Owner owner,
               std::shared_ptr<const SectionLightVolume> data,
               glm::ivec3 section_origin);
  bool Release(Handle handle);
  bool IsLive(Handle handle) const;
  bool HasResidentPage(Handle handle) const;
  /// Stable metadata without a resident page. The shader rejects this entry;
  /// its owner must stage the certified CPU field for each ordered fallback.
  std::optional<Handle> CreateDeferred();
  bool Defer(Handle handle);
  /// Draw immediately before any later stage. A reserved metadata entry and
  /// atlas page are reused in GL command order, independently of resident load.
  std::optional<Handle>
  StageForOrderedDraw(SectionLightAtlas::Owner owner,
                      std::shared_ptr<const SectionLightVolume> data,
                      glm::ivec3 section_origin);
  void CollectRetired() { Atlas.CollectRetired(); }
  bool Bind(unsigned atlas_unit, unsigned table_unit) const;
  std::array<uint32_t, 2> ShaderIncarnation() const;
  const SectionLightAtlas &GetAtlas() const { return Atlas; }
  uint32_t Capacity() const { return static_cast<uint32_t>(Slots.size()); }
  /// Common GL3.3 / GLES3 integer-texture interface. Inject after #version and
  /// GLES precision declarations. A failed lookup is explicit, never sky zero.
  static const char *ShaderInterface();

private:
  struct Slot
  {
    SectionLightAtlas::Page page;
    uint32_t generation{0};
    bool live{false};
  };
  bool Upload(uint32_t index, uint32_t generation,
              const SectionLightAtlas::Page *page, glm::ivec3 origin);
  SectionLightAtlas Atlas;
  GLuint Texture{0};
  GLint TextureUnits{0};
  int BindingsPerRow{0};
  uint64_t Incarnation{0};
  std::vector<Slot> Slots;
  std::vector<uint32_t> Free;
  uint32_t OrderedIndex{std::numeric_limits<uint32_t>::max()};
};
} // namespace cutum
