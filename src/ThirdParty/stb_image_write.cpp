#include "Core/Memory/ScopedPngEncoder.h"
#include "Core/Memory/StbiAllocation.h"
#include "Gui/Cache/InventoryIconPng.h"
#include <climits>
#include <cstdint>

namespace
{
// The stb encoder may assume stretchy-buffer growth succeeds. A scoped
// encode unwinds on refusal and retires every allocation still in flight.
class ScopedPngAllocations;
thread_local ScopedPngAllocations *ActivePng = nullptr;

struct alignas(std::max_align_t) PngBlock
{
  PngBlock *Previous;
  PngBlock *Next;
};

class ScopedPngAllocations
{
public:
  explicit ScopedPngAllocations(cutum::PngAllocationObserver observer)
      : Outer(ActivePng), Observer(observer) { ActivePng = this; }
  ~ScopedPngAllocations()
  {
    while (Head)
      Release(Head + 1);
    ActivePng = Outer;
  }
  void Deactivate() noexcept { ActivePng = Outer; }

  void *Resize(void *pointer, std::size_t bytes)
  {
    constexpr auto prefix = sizeof(PngBlock) + cutum::StbiAllocationHeaderBytes();
    if (bytes > SIZE_MAX - prefix)
      throw std::bad_alloc{};
    const auto requested = bytes + prefix;
    Observe(requested);
    if (!pointer)
    {
      void *memory = cutum::StbiAllocate(bytes + sizeof(PngBlock));
      if (!memory)
      {
        Observe(0);
        throw std::bad_alloc{};
      }
      LiveRequestBytes += requested;
      auto *block = ::new (memory) PngBlock{nullptr, Head};
      Link(block);
      Observe(0);
      return block + 1;
    }

    auto *old = static_cast<PngBlock *>(pointer) - 1;
    const auto links = *old;
    const auto previous = cutum::InspectStbiAllocation(old).request_bytes;
    void *memory = cutum::StbiReallocate(old, bytes + sizeof(PngBlock));
    if (!memory)
    {
      Observe(0);
      throw std::bad_alloc{};
    }
    LiveRequestBytes = LiveRequestBytes - previous + requested;
    auto *block = ::new (memory) PngBlock(links);
    Link(block);
    Observe(0);
    return block + 1;
  }

  void Release(void *pointer) noexcept
  {
    if (!pointer)
      return;
    auto *block = static_cast<PngBlock *>(pointer) - 1;
    if (block->Previous)
      block->Previous->Next = block->Next;
    else
      Head = block->Next;
    if (block->Next)
      block->Next->Previous = block->Previous;
    LiveRequestBytes -= cutum::InspectStbiAllocation(block).request_bytes;
    cutum::StbiFree(block);
    Observe(0);
  }

private:
  void Link(PngBlock *block) noexcept
  {
    if (block->Previous)
      block->Previous->Next = block;
    else
      Head = block;
    if (block->Next)
      block->Next->Previous = block;
  }

  void Observe(std::size_t pending) const noexcept
  {
    if (Observer.sample)
      Observer.sample(Observer.context, LiveRequestBytes, pending);
  }

  PngBlock *Head = nullptr;
  ScopedPngAllocations *Outer;
  cutum::PngAllocationObserver Observer;
  std::size_t LiveRequestBytes{};
};

void *WriteAllocate(std::size_t bytes)
{
  return ActivePng ? ActivePng->Resize(nullptr, bytes) : std::malloc(bytes);
}
void *WriteReallocate(void *pointer, std::size_t bytes)
{
  return ActivePng ? ActivePng->Resize(pointer, bytes) : std::realloc(pointer, bytes);
}
void WriteFree(void *pointer)
{
  if (ActivePng)
    ActivePng->Release(pointer);
  else
    std::free(pointer);
}
} // namespace

#define STBIW_MALLOC(bytes) WriteAllocate(bytes)
#define STBIW_REALLOC(pointer, bytes) WriteReallocate(pointer, bytes)
#define STBIW_FREE(pointer) WriteFree(pointer)
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "ThirdParty/stb_image_write.h"

std::size_t cutum::RgbaPngInputBytes(int width, int height) noexcept
{
  if (width <= 0 || height <= 0)
    return 0;
  const auto row = std::uint64_t(width) * 4;
  // The filter heuristic accumulates absolute signed bytes in an int.
  if (row > INT_MAX / 128)
    return 0;
  const auto filtered = (row + 1) * std::uint64_t(height);
  // Bound fixed Deflate literals, block overhead and the next stretchy-buffer
  // doubling before entering stb's signed length arithmetic.
  const auto fixedDeflate = (9 * filtered + 7) / 8 + 16;
  const auto storedDeflate = filtered + 5 * ((filtered + 32766) / 32767) + 6;
  if (std::max(fixedDeflate, storedDeflate) > (INT_MAX - 64) / 2)
    return 0;
  return static_cast<std::size_t>(row * std::uint64_t(height));
}

bool cutum::EncodeRgbaPng(RgbaPngView input, void *context, PngConsumer consume,
                         PngAllocationObserver observer)
{
  const auto required = RgbaPngInputBytes(input.width, input.height);
  if (!required || !input.pixels || !consume || input.bytes != required)
    return false;
  ScopedPngAllocations allocations(observer);
  int length = 0;
  const unsigned char *png = nullptr;
  try
  {
    // Keep allocator exceptions within C++ linkage in MSVC /EHsc builds.
    png = stbiw__png_to_mem(input.pixels, input.width * 4, input.width,
                           input.height, 4, input.bottomUp, &length);
  }
  catch (const std::bad_alloc &)
  {
    return false;
  }
  if (!png || length <= 0)
    return false;
  const auto *block = reinterpret_cast<const PngBlock *>(png) - 1;
  allocations.Deactivate();
  return consume(context, png, std::size_t(length), InspectStbiAllocation(block).request_bytes);
}

bool cutum::EncodeInventoryIconPng(const unsigned char *rgba, int size, void *context,
                                  InventoryIconPngConsumer consume)
{
  if (size <= 0 || size > (INT_MAX / 16) / size)
    return false;
  return EncodeRgbaPng({rgba, RgbaPngInputBytes(size, size), size, size,
                        stbi__flip_vertically_on_write != 0}, context, consume);
}
