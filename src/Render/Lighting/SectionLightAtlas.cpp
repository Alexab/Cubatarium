#include "Render/Lighting/SectionLightAtlas.h"
#include <algorithm>
#include <atomic>
#include <limits>

namespace cutum
{
namespace
{
std::atomic<uint64_t> NextAtlasIncarnation{1};

// Uploads must be independent of a caller's PBO, strides, skips and alignment.
// Restoring them also makes an atlas upload safe between ordinary mesh passes.
struct UploadState
{
  GLint texture, buffer, alignment, row_length, image_height;
  GLint skip_pixels, skip_rows, skip_images;
  UploadState()
  {
    glGetIntegerv(GL_TEXTURE_BINDING_3D, &texture);
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
    glBindTexture(GL_TEXTURE_3D, static_cast<GLuint>(texture));
    glBindBuffer(GL_PIXEL_UNPACK_BUFFER, static_cast<GLuint>(buffer));
    glPixelStorei(GL_UNPACK_ALIGNMENT, alignment);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, row_length);
    glPixelStorei(GL_UNPACK_IMAGE_HEIGHT, image_height);
    glPixelStorei(GL_UNPACK_SKIP_PIXELS, skip_pixels);
    glPixelStorei(GL_UNPACK_SKIP_ROWS, skip_rows);
    glPixelStorei(GL_UNPACK_SKIP_IMAGES, skip_images);
  }
};
} // namespace

SectionLightAtlas::Layout
SectionLightAtlas::Layout::Choose(int max_texture_width, size_t byte_budget,
                                  uint32_t page_limit)
{
  Layout result;
  constexpr size_t page_bytes = SectionLightVolume::CellCount;
  const size_t available =
      std::min<size_t>(byte_budget / page_bytes, page_limit);
  const int device_tiles =
      std::max(0, max_texture_width / SectionLightVolume::Width);
  // At most 1625 tiles/axis can fit uint32_t page indices. All products below
  // use size_t and this cap, even for intentionally hostile limits in tests.
  const int limit = std::min(device_tiles, 1625);
  for (int n = 1; n <= limit; ++n)
  {
    const size_t capacity = size_t(n) * n * n;
    if (capacity > available)
      break;
    result.tiles_per_axis = n;
    result.texture_width = n * SectionLightVolume::Width;
    result.capacity = static_cast<uint32_t>(capacity);
    result.bytes = capacity * page_bytes;
  }
  return result;
}

size_t SectionLightAtlas::OwnerHash::operator()(const Owner &owner) const
{
  size_t value = std::hash<uint64_t>{}(owner.world_epoch);
  for (const auto part : {owner.field_incarnation, owner.field_revision})
    value ^= std::hash<uint64_t>{}(part) + size_t(0x9e3779b9) + (value << 6) +
             (value >> 2);
  return value;
}

SectionLightAtlas::~SectionLightAtlas() { Reset(); }

bool SectionLightAtlas::Initialize(size_t byte_budget, uint32_t page_limit,
                                   bool reserve_ordered_page)
{
  if (Texture)
    return false;
  GLint max_width = 0;
  glGetIntegerv(GL_MAX_3D_TEXTURE_SIZE, &max_width);
  glGetIntegerv(GL_MAX_COMBINED_TEXTURE_IMAGE_UNITS, &TextureUnits);
  const auto layout = Layout::Choose(max_width, byte_budget, page_limit);
  if (!layout.capacity || TextureUnits <= 0)
    return false;
  GLuint candidate = 0;
  bool allocated = false;
  {
    UploadState saved;
    glGenTextures(1, &candidate);
    glBindTexture(GL_TEXTURE_3D, candidate);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
    glTexImage3D(GL_TEXTURE_3D, 0, GL_R8UI, layout.texture_width,
                 layout.texture_width, layout.texture_width, 0, GL_RED_INTEGER,
                 GL_UNSIGNED_BYTE, nullptr);
    allocated = candidate != 0 && glGetError() == GL_NO_ERROR;
  }
  if (!allocated)
  {
    if (candidate)
      glDeleteTextures(1, &candidate);
    return false;
  }
  Texture = candidate;
  AtlasLayout = layout;
  Incarnation = NextAtlasIncarnation.fetch_add(1, std::memory_order_relaxed);
  Slots.resize(layout.capacity);
  OrderedIndex = reserve_ordered_page ? layout.capacity - 1
                                      : std::numeric_limits<uint32_t>::max();
  if (reserve_ordered_page)
    Slots[OrderedIndex].state = State::Ordered;
  Free.reserve(layout.capacity);
  for (uint32_t n = layout.capacity; n > 0; --n)
    if (n - 1 != OrderedIndex)
      Free.push_back(n - 1);
  return true;
}

void SectionLightAtlas::Reset()
{
  for (auto &slot : Slots)
    if (slot.fence)
      glDeleteSync(slot.fence);
  if (Texture)
    glDeleteTextures(1, &Texture);
  Texture = 0;
  TextureUnits = 0;
  AtlasLayout = {};
  Incarnation = 0;
  Uploads = UploadedBytes = 0;
  OrderedUploads = 0;
  OrderedIndex = std::numeric_limits<uint32_t>::max();
  Slots.clear();
  Free.clear();
  Owners.clear();
}

glm::ivec3 SectionLightAtlas::SlotOrigin(uint32_t slot) const
{
  const auto n = static_cast<uint32_t>(AtlasLayout.tiles_per_axis);
  return glm::ivec3(slot % n, (slot / n) % n, slot / (n * n)) *
         SectionLightVolume::Width;
}

bool SectionLightAtlas::Upload(uint32_t slot, const SectionLightVolume &volume)
{
  UploadState saved;
  glBindTexture(GL_TEXTURE_3D, Texture);
  const auto origin = SlotOrigin(slot);
  constexpr int width = SectionLightVolume::Width;
  glTexSubImage3D(GL_TEXTURE_3D, 0, origin.x, origin.y, origin.z, width, width,
                  width, GL_RED_INTEGER, GL_UNSIGNED_BYTE, volume.cells.data());
  return glGetError() == GL_NO_ERROR;
}

std::optional<SectionLightAtlas::Page>
SectionLightAtlas::Acquire(Owner owner,
                           std::shared_ptr<const SectionLightVolume> volume)
{
  if (!Texture || !volume)
    return std::nullopt;
  if (const auto found = Owners.find(owner); found != Owners.end())
  {
    auto &slot = Slots[found->second];
    // A reused revision must never silently change immutable page contents.
    if ((slot.volume != volume && slot.volume->cells != volume->cells) ||
        slot.references == std::numeric_limits<uint32_t>::max())
      return std::nullopt;
    ++slot.references;
    return Page{Incarnation, found->second, slot.generation, true};
  }
  if (Free.empty())
    return std::nullopt;
  const auto index = Free.back();
  if (!Upload(index, *volume))
    return std::nullopt;
  Free.pop_back();
  auto &slot = Slots[index];
  slot.owner = owner;
  slot.references = 1;
  slot.volume = std::move(volume);
  slot.state = State::Live;
  Owners.emplace(owner, index);
  ++Uploads;
  UploadedBytes += SectionLightVolume::CellCount;
  return Page{Incarnation, index, slot.generation, true};
}

bool SectionLightAtlas::IsLive(Page page) const
{
  return page.valid && Texture && page.atlas_incarnation == Incarnation &&
         page.slot < Slots.size() &&
         (Slots[page.slot].state == State::Live ||
          (Slots[page.slot].state == State::Ordered &&
           Slots[page.slot].volume)) &&
         Slots[page.slot].generation == page.generation;
}

std::optional<SectionLightAtlas::Page> SectionLightAtlas::StageForOrderedDraw(
    Owner owner, std::shared_ptr<const SectionLightVolume> volume)
{
  if (!Texture || !volume || OrderedIndex >= Slots.size() ||
      glGetError() != GL_NO_ERROR)
    return std::nullopt;
  auto &slot = Slots[OrderedIndex];
  if (slot.generation == std::numeric_limits<uint32_t>::max() ||
      (slot.volume && slot.owner == owner && slot.volume != volume &&
       slot.volume->cells != volume->cells))
    return std::nullopt;
  ++slot.generation;
  slot.volume.reset();
  if (!Upload(OrderedIndex, *volume))
    return std::nullopt;
  slot.owner = owner;
  slot.volume = std::move(volume);
  ++Uploads;
  ++OrderedUploads;
  UploadedBytes += SectionLightVolume::CellCount;
  return Page{Incarnation, OrderedIndex, slot.generation, true};
}

std::optional<glm::ivec3> SectionLightAtlas::Origin(Page page) const
{
  return IsLive(page) ? std::optional(SlotOrigin(page.slot)) : std::nullopt;
}

bool SectionLightAtlas::Release(Page page)
{
  if (!IsLive(page) || page.slot == OrderedIndex)
    return false;
  auto &slot = Slots[page.slot];
  if (--slot.references)
    return true;
  Owners.erase(slot.owner);
  slot.volume.reset();
  slot.fence = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
  // A failed fence cannot justify reuse. Quarantine until explicit reset.
  slot.state = slot.fence ? State::Retired : State::Quarantined;
  return true;
}

void SectionLightAtlas::CollectRetired()
{
  for (uint32_t index = 0; index < Slots.size(); ++index)
  {
    auto &slot = Slots[index];
    if (slot.state != State::Retired)
      continue;
    const auto status = glClientWaitSync(slot.fence, 0, 0);
    if (status == GL_TIMEOUT_EXPIRED)
      continue;
    glDeleteSync(slot.fence);
    slot.fence = nullptr;
    if (status == GL_WAIT_FAILED ||
        slot.generation == std::numeric_limits<uint32_t>::max())
    {
      slot.state = State::Quarantined;
      continue;
    }
    ++slot.generation;
    slot.state = State::Free;
    Free.push_back(index);
  }
}

bool SectionLightAtlas::Bind(unsigned texture_unit) const
{
  if (!Texture || texture_unit >= static_cast<unsigned>(TextureUnits))
    return false;
  GLint old_active = 0;
  glGetIntegerv(GL_ACTIVE_TEXTURE, &old_active);
  glActiveTexture(GL_TEXTURE0 + texture_unit);
  glBindTexture(GL_TEXTURE_3D, Texture);
  glActiveTexture(static_cast<GLenum>(old_active));
  return true;
}

SectionLightAtlas::Statistics SectionLightAtlas::GetStatistics() const
{
  Statistics result;
  result.uploads = Uploads;
  result.uploaded_bytes = UploadedBytes;
  result.ordered_uploads = OrderedUploads;
  result.ordered_pages = OrderedIndex < Slots.size() ? 1 : 0;
  for (const auto &slot : Slots)
  {
    result.live += slot.state == State::Live;
    result.retired += slot.state == State::Retired;
    result.quarantined += slot.state == State::Quarantined;
  }
  return result;
}
} // namespace cutum
