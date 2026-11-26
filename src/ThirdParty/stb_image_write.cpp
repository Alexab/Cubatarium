#include "Core/Memory/StbiAllocation.h"
#include "Gui/Cache/InventoryIconPng.h"
#include <climits>

namespace
{
// The stb encoder may assume stretchy-buffer growth succeeds. The synchronous
// icon route unwinds on refusal and retires every allocation still in flight.
class IconPngAllocations;
thread_local IconPngAllocations *ActiveIconPng = nullptr;

struct alignas(std::max_align_t) IconPngBlock
{
  IconPngBlock *Previous;
  IconPngBlock *Next;
};

class IconPngAllocations
{
public:
  IconPngAllocations() : Outer(ActiveIconPng) { ActiveIconPng = this; }
  ~IconPngAllocations()
  {
    while (Head)
      Release(Head + 1);
    ActiveIconPng = Outer;
  }
  void Deactivate() noexcept { ActiveIconPng = Outer; }

  void *Resize(void *pointer, std::size_t bytes)
  {
    if (bytes > SIZE_MAX - sizeof(IconPngBlock))
      throw std::bad_alloc{};
    if (!pointer)
    {
      void *memory = cutum::StbiAllocate(bytes + sizeof(IconPngBlock));
      if (!memory)
        throw std::bad_alloc{};
      auto *block = ::new (memory) IconPngBlock{nullptr, Head};
      Link(block);
      return block + 1;
    }

    auto *old = static_cast<IconPngBlock *>(pointer) - 1;
    const auto links = *old;
    void *memory = cutum::StbiReallocate(old, bytes + sizeof(IconPngBlock));
    if (!memory)
      throw std::bad_alloc{};
    auto *block = ::new (memory) IconPngBlock(links);
    Link(block);
    return block + 1;
  }

  void Release(void *pointer) noexcept
  {
    if (!pointer)
      return;
    auto *block = static_cast<IconPngBlock *>(pointer) - 1;
    if (block->Previous)
      block->Previous->Next = block->Next;
    else
      Head = block->Next;
    if (block->Next)
      block->Next->Previous = block->Previous;
    cutum::StbiFree(block);
  }

private:
  void Link(IconPngBlock *block) noexcept
  {
    if (block->Previous)
      block->Previous->Next = block;
    else
      Head = block;
    if (block->Next)
      block->Next->Previous = block;
  }

  IconPngBlock *Head = nullptr;
  IconPngAllocations *Outer;
};

void *WriteAllocate(std::size_t bytes)
{
  return ActiveIconPng ? ActiveIconPng->Resize(nullptr, bytes) : std::malloc(bytes);
}
void *WriteReallocate(void *pointer, std::size_t bytes)
{
  return ActiveIconPng ? ActiveIconPng->Resize(pointer, bytes) : std::realloc(pointer, bytes);
}
void WriteFree(void *pointer)
{
  if (ActiveIconPng)
    ActiveIconPng->Release(pointer);
  else
    std::free(pointer);
}
} // namespace

#define STBIW_MALLOC(bytes) WriteAllocate(bytes)
#define STBIW_REALLOC(pointer, bytes) WriteReallocate(pointer, bytes)
#define STBIW_FREE(pointer) WriteFree(pointer)
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "ThirdParty/stb_image_write.h"

bool cutum::EncodeInventoryIconPng(const unsigned char *rgba, int size, void *context,
                                   InventoryIconPngConsumer consume)
{
  // The filter, zlib and chunk lengths use signed int in stb.
  if (!rgba || !consume || size <= 0 || size > (INT_MAX / 16) / size)
    return false;
  IconPngAllocations allocations;
  int length = 0;
  const unsigned char *png = nullptr;
  try
  {
    // Keep allocator exceptions within C++ linkage in MSVC /EHsc builds.
    png = stbiw__png_to_mem(rgba, size * 4, size, size, 4, &length);
  }
  catch (const std::bad_alloc &)
  {
    return false;
  }
  if (!png || length <= 0)
    return false;
  const auto *block = reinterpret_cast<const IconPngBlock *>(png) - 1;
  allocations.Deactivate();
  return consume(context, png, size_t(length), InspectStbiAllocation(block).request_bytes);
}
