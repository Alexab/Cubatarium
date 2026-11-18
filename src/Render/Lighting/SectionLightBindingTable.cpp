#include "Render/Lighting/SectionLightBindingTable.h"
#include <algorithm>
#include <atomic>
#include <cstring>
#include <limits>

namespace cutum
{
namespace
{
std::atomic<uint64_t> NextBindingIncarnation{1};

struct UploadState
{
  GLint texture, buffer, alignment, row_length, image_height;
  GLint skip_pixels, skip_rows, skip_images;
  UploadState()
  {
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &texture);
    glGetIntegerv(GL_PIXEL_UNPACK_BUFFER_BINDING, &buffer);
    glGetIntegerv(GL_UNPACK_ALIGNMENT, &alignment);
    glGetIntegerv(GL_UNPACK_ROW_LENGTH, &row_length);
    glGetIntegerv(GL_UNPACK_IMAGE_HEIGHT, &image_height);
    glGetIntegerv(GL_UNPACK_SKIP_PIXELS, &skip_pixels);
    glGetIntegerv(GL_UNPACK_SKIP_ROWS, &skip_rows);
    glGetIntegerv(GL_UNPACK_SKIP_IMAGES, &skip_images);
    glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    glPixelStorei(GL_UNPACK_IMAGE_HEIGHT, 0);
    glPixelStorei(GL_UNPACK_SKIP_PIXELS, 0);
    glPixelStorei(GL_UNPACK_SKIP_ROWS, 0);
    glPixelStorei(GL_UNPACK_SKIP_IMAGES, 0);
  }
  ~UploadState()
  {
    glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(texture));
    glBindBuffer(GL_PIXEL_UNPACK_BUFFER, static_cast<GLuint>(buffer));
    glPixelStorei(GL_UNPACK_ALIGNMENT, alignment);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, row_length);
    glPixelStorei(GL_UNPACK_IMAGE_HEIGHT, image_height);
    glPixelStorei(GL_UNPACK_SKIP_PIXELS, skip_pixels);
    glPixelStorei(GL_UNPACK_SKIP_ROWS, skip_rows);
    glPixelStorei(GL_UNPACK_SKIP_IMAGES, skip_images);
  }
};

uint32_t SignedBits(int value)
{
  static_assert(sizeof(value) == sizeof(uint32_t));
  uint32_t result;
  std::memcpy(&result, &value, sizeof(result));
  return result;
}
} // namespace

std::array<uint32_t, 4> SectionLightBindingTable::Handle::ShaderWords() const
{
  // Zero is a valid index/generation, but table incarnation zero is reserved.
  return {index, generation, valid ? uint32_t(table_incarnation) : 0u,
          valid ? uint32_t(table_incarnation >> 32) : 0u};
}

bool SectionLightBindingTable::Handle::operator==(const Handle &other) const
{
  return index == other.index && generation == other.generation &&
         table_incarnation == other.table_incarnation && valid == other.valid;
}

SectionLightBindingTable::~SectionLightBindingTable() { Reset(); }

bool SectionLightBindingTable::Initialize(uint32_t binding_limit,
                                          size_t atlas_bytes,
                                          uint32_t page_limit,
                                          bool reserve_ordered_draw)
{
  if (Texture || !binding_limit || binding_limit > 1024u * 1024u)
    return false;
  GLint width = 0;
  glGetIntegerv(GL_MAX_TEXTURE_SIZE, &width);
  glGetIntegerv(GL_MAX_COMBINED_TEXTURE_IMAGE_UNITS, &TextureUnits);
  if (width < 2 || TextureUnits < 2)
    return false;
  // Keep metadata rows bounded instead of allocating a device-wide texture
  // for the usual small table; two adjacent RGBA texels never cross a row.
  const uint64_t per_row =
      std::min<uint64_t>({binding_limit, uint64_t(width / 2), 128});
  const uint64_t capacity =
      std::min<uint64_t>(binding_limit, per_row * uint64_t(width));
  const int height = static_cast<int>((capacity + per_row - 1) / per_row);
  const size_t texels = size_t(per_row) * height * 2;
  // Hostile caller limits must not cause an unbounded CPU/driver allocation.
  if (texels > 2u * 1024u * 1024u ||
      !Atlas.Initialize(atlas_bytes, page_limit, reserve_ordered_draw))
    return false;
  bool allocated = false;
  {
    UploadState saved;
    std::vector<uint32_t> zeros(texels * 4, 0);
    glGenTextures(1, &Texture);
    glBindTexture(GL_TEXTURE_2D, Texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32UI, static_cast<int>(per_row * 2),
                 height, 0, GL_RGBA_INTEGER, GL_UNSIGNED_INT, zeros.data());
    allocated = Texture && glGetError() == GL_NO_ERROR;
  }
  if (!allocated)
  {
    Reset();
    return false;
  }
  Incarnation = NextBindingIncarnation.fetch_add(1, std::memory_order_relaxed);
  if (!Incarnation)
  {
    Reset();
    return false;
  }
  BindingsPerRow = static_cast<int>(per_row);
  Slots.resize(static_cast<size_t>(capacity));
  OrderedIndex = reserve_ordered_draw ? Capacity() - 1
                                      : std::numeric_limits<uint32_t>::max();
  Free.reserve(Slots.size());
  for (uint32_t n = Capacity(); n > 0; --n)
    if (n - 1 != OrderedIndex)
      Free.push_back(n - 1);
  return true;
}

void SectionLightBindingTable::Reset()
{
  // Deletion follows all prior draws in the owning GL command stream.
  if (Texture)
    glDeleteTextures(1, &Texture);
  Texture = 0;
  Atlas.Reset();
  Slots.clear();
  Free.clear();
  BindingsPerRow = TextureUnits = 0;
  Incarnation = 0;
  OrderedIndex = std::numeric_limits<uint32_t>::max();
}

bool SectionLightBindingTable::Upload(uint32_t index, uint32_t generation,
                                      const SectionLightAtlas::Page *page,
                                      glm::ivec3 origin)
{
  // A prior caller error must not be mistaken for failure AFTER a successful
  // update of a live entry. Reject before mutating the published GPU table.
  if (glGetError() != GL_NO_ERROR)
    return false;
  std::array<uint32_t, 8> words{generation, 0};
  if (page)
  {
    const auto offset = Atlas.Origin(*page);
    if (!offset)
      return false;
    words = {generation,           1,
             uint32_t(offset->x),  uint32_t(offset->y),
             uint32_t(offset->z),  SignedBits(origin.x),
             SignedBits(origin.y), SignedBits(origin.z)};
  }
  UploadState saved;
  glBindTexture(GL_TEXTURE_2D, Texture);
  glTexSubImage2D(GL_TEXTURE_2D, 0, (index % BindingsPerRow) * 2,
                  index / BindingsPerRow, 2, 1, GL_RGBA_INTEGER,
                  GL_UNSIGNED_INT, words.data());
  return glGetError() == GL_NO_ERROR;
}

std::optional<SectionLightBindingTable::Handle>
SectionLightBindingTable::Create(SectionLightAtlas::Owner owner,
                                 std::shared_ptr<const SectionLightVolume> data,
                                 glm::ivec3 section_origin)
{
  if (!Texture || Free.empty())
    return std::nullopt;
  const auto page = Atlas.Acquire(owner, std::move(data));
  if (!page)
    return std::nullopt;
  const auto index = Free.back();
  auto &slot = Slots[index];
  if (!Upload(index, slot.generation, &*page, section_origin))
  {
    Atlas.Release(*page);
    return std::nullopt;
  }
  Free.pop_back();
  slot.live = true;
  slot.page = *page;
  return Handle{index, slot.generation, Incarnation, true};
}

bool SectionLightBindingTable::IsLive(Handle handle) const
{
  return handle.valid && Texture && handle.table_incarnation == Incarnation &&
         handle.index < Slots.size() && Slots[handle.index].live &&
         Slots[handle.index].generation == handle.generation;
}

bool SectionLightBindingTable::HasResidentPage(Handle handle) const
{
  return IsLive(handle) && Atlas.IsLive(Slots[handle.index].page);
}

std::optional<SectionLightBindingTable::Handle>
SectionLightBindingTable::CreateDeferred()
{
  if (!Texture || OrderedIndex >= Slots.size() || Free.empty())
    return std::nullopt;
  const auto index = Free.back();
  auto &slot = Slots[index];
  if (!Upload(index, slot.generation, nullptr, {}))
    return std::nullopt;
  Free.pop_back();
  slot.live = true;
  slot.page = {};
  return Handle{index, slot.generation, Incarnation, true};
}

bool SectionLightBindingTable::Defer(Handle handle)
{
  if (OrderedIndex >= Slots.size() || !IsLive(handle) ||
      handle.index == OrderedIndex ||
      !Upload(handle.index, handle.generation, nullptr, {}))
    return false;
  auto &slot = Slots[handle.index];
  if (slot.page.valid)
    Atlas.Release(slot.page);
  slot.page = {};
  return true;
}

bool SectionLightBindingTable::Replace(
    Handle handle, SectionLightAtlas::Owner owner,
    std::shared_ptr<const SectionLightVolume> data, glm::ivec3 section_origin)
{
  if (!IsLive(handle) || handle.index == OrderedIndex)
    return false;
  const auto page = Atlas.Acquire(owner, std::move(data));
  if (!page)
    return false;
  auto &slot = Slots[handle.index];
  if (!Upload(handle.index, handle.generation, &*page, section_origin))
  {
    Atlas.Release(*page);
    return false;
  }
  const auto previous = slot.page;
  slot.page = *page;
  if (previous.valid)
    Atlas.Release(previous);
  return true;
}

bool SectionLightBindingTable::Release(Handle handle)
{
  if (!IsLive(handle) || handle.index == OrderedIndex ||
      !Upload(handle.index, handle.generation, nullptr, {}))
    return false;
  auto &slot = Slots[handle.index];
  if (slot.page.valid)
    Atlas.Release(slot.page);
  slot.live = false;
  slot.page = {};
  // Generation exhaustion quarantines this index until reset.
  if (slot.generation != std::numeric_limits<uint32_t>::max())
  {
    ++slot.generation;
    Free.push_back(handle.index);
  }
  return true;
}

std::optional<SectionLightBindingTable::Handle>
SectionLightBindingTable::StageForOrderedDraw(
    SectionLightAtlas::Owner owner,
    std::shared_ptr<const SectionLightVolume> data, glm::ivec3 section_origin)
{
  if (!Texture || !data || OrderedIndex >= Slots.size() ||
      glGetError() != GL_NO_ERROR)
    return std::nullopt;
  auto &slot = Slots[OrderedIndex];
  if (slot.generation == std::numeric_limits<uint32_t>::max())
    return std::nullopt;
  const auto generation = slot.generation + 1;
  // Invalidate before overwriting shared texels. If a later upload fails,
  // neither the old handle nor an uncommitted successor may sample new bytes.
  if (!Upload(OrderedIndex, generation, nullptr, {}))
    return std::nullopt;
  slot.live = false;
  slot.generation = generation;
  slot.page = {};
  const auto page = Atlas.StageForOrderedDraw(owner, std::move(data));
  if (!page || !Upload(OrderedIndex, generation, &*page, section_origin))
    return std::nullopt;
  slot.page = *page;
  slot.live = true;
  return Handle{OrderedIndex, generation, Incarnation, true};
}

bool SectionLightBindingTable::Bind(unsigned atlas_unit,
                                    unsigned table_unit) const
{
  if (!Texture || atlas_unit == table_unit ||
      atlas_unit >= static_cast<unsigned>(TextureUnits) ||
      table_unit >= static_cast<unsigned>(TextureUnits))
    return false;
  if (!Atlas.Bind(atlas_unit))
    return false;
  GLint active = 0;
  glGetIntegerv(GL_ACTIVE_TEXTURE, &active);
  glActiveTexture(GL_TEXTURE0 + table_unit);
  glBindTexture(GL_TEXTURE_2D, Texture);
  glActiveTexture(static_cast<GLenum>(active));
  return true;
}

std::array<uint32_t, 2> SectionLightBindingTable::ShaderIncarnation() const
{
  return {uint32_t(Incarnation), uint32_t(Incarnation >> 32)};
}

std::string SectionLightBindingTable::ExpandShaderInterface(std::string source)
{
  const std::string marker = "/* CUBATARIUM_SECTION_LIGHT_INTERFACE */";
  const auto at = source.find(marker);
  if (at != std::string::npos)
    source.replace(at, marker.size(), ShaderInterface());
  return source;
}

const char *SectionLightBindingTable::ShaderInterface()
{
  return R"glsl(
uniform highp usampler3D uSectionLightAtlas;
uniform highp usampler2D uSectionLightBindings;
uniform highp uvec2 uSectionLightTableIncarnation;
bool sectionLightFetch(highp uvec4 handle, highp ivec3 cell,
                       out highp uint packedLight)
{
    packedLight = 0u;
    if (all(equal(handle.zw, uvec2(0u))) ||
        any(notEqual(handle.zw, uSectionLightTableIncarnation))) return false;
    highp ivec2 size = textureSize(uSectionLightBindings, 0);
    highp uint rowWidth = uint(size.x / 2);
    if (rowWidth == 0u || handle.x / rowWidth >= uint(size.y)) return false;
    highp ivec2 at = ivec2(int(handle.x % rowWidth) * 2,
                           int(handle.x / rowWidth));
    highp uvec4 a = texelFetch(uSectionLightBindings, at, 0);
    if (a.y != 1u || a.x != handle.y) return false;
    highp uvec4 b = texelFetch(uSectionLightBindings, at + ivec2(1,0), 0);
    // Decode signed origins explicitly; uint->int conversion of high-bit
    // values is not used as an implicit signed-coordinate contract.
    highp ivec3 origin = ivec3(b.yzw & uvec3(0x7fffffffu)) +
                        ivec3(b.yzw >> 31u) * (-2147483647 - 1);
    // Unsigned subtraction preserves individual cells even beyond float's
    // integer precision. Check the mathematical sign as well, so opposite
    // INT limits cannot wrap into a falsely adjacent halo cell.
    highp uvec3 delta = uvec3(cell) - b.yzw;
    if (any(notEqual(lessThan(cell, origin),
                      greaterThan(delta, uvec3(0x7fffffffu))))) return false;
    highp uvec3 shifted = delta + uvec3(2u);
    if (any(greaterThanEqual(shifted, uvec3(20u)))) return false;
    highp ivec3 local = ivec3(shifted);
    packedLight = texelFetch(uSectionLightAtlas,
                   ivec3(a.zw, b.x) + local, 0).r;
    return true;
}

// The meshes use half-centered voxel faces. Resolve the two tangent axes per
// fragment, so a merged quad can span cells with different physical light.
// Resolve the face plane to its nearest grid boundary before choosing the
// outside cell; this avoids a tiny directional epsilon disappearing at large
// signed coordinates or flickering across an interpolated plane.
bool sectionLightSurfaceFetch(highp uvec4 handle, highp vec3 worldPos,
                              highp int face, out highp uint packedLight)
{
    packedLight = 0u;
    if ((face != 127 && (face < 0 || face > 5)) || any(isnan(worldPos)) ||
        any(isinf(worldPos)) ||
        any(lessThan(worldPos, vec3(-2147483000.0))) ||
        any(greaterThan(worldPos, vec3(2147483000.0)))) return false;
    highp ivec3 cell = ivec3(floor(worldPos + vec3(0.5)));
    // Explicit-UV CPU decor has no voxel face normal. Its fragments sample
    // their actual occupied cells; instanced decor retains its center policy.
    if (face == 127) return sectionLightFetch(handle, cell, packedLight);
    highp int axis = (face == 0 || face == 2) ? 2 :
                      ((face == 1 || face == 3) ? 0 : 1);
    highp int boundary = int(floor(worldPos[axis] + 1.0));
    cell[axis] = boundary - ((face == 2 || face == 3 || face == 5) ? 1 : 0);
    return sectionLightFetch(handle, cell, packedLight);
}
)glsl";
}
} // namespace cutum
