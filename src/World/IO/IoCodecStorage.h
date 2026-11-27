#pragma once

#include <cstddef>

namespace cutum
{
enum class IoCodecKind
{
  BinaryWrite, BinaryRead, JsonWrite, JsonRead, ReceiptEncode, ReceiptWrite, ReceiptRead, FlagsWrite,
  ReceiptValidation, ReceiptChecksum, DiskDirectoryEntry, Count
};

enum class IoCodecWindow { JsonParse, JsonDump, ReceiptValidate, ReceiptHash, Count };

struct IoCodecStorage
{
  IoCodecKind kind{IoCodecKind::Count};
  size_t scratch_bytes{}, input_bytes{}, output_bytes{}, stack_buffer_bytes{};
  bool supported{};
};

// Codec samples are synchronous and refer to buffers that are still alive.
// DiskDirectoryEntry is a historical sample delivered after releasing the index lock.
struct IoCodecProbe
{
  void *context;
  void (*sample)(void *, const IoCodecStorage &) noexcept;
  void (*window)(void *, IoCodecWindow, bool entering) noexcept {};
};

// Window callbacks bracket a real operation, including its exception path.
// Returning a prvalue preserves ownership of the codec's actual output.
template<class Operation>
decltype(auto) RunIoCodecWindow(const IoCodecProbe *probe, IoCodecWindow kind,
                                Operation &&operation)
{
  if (!probe || !probe->window)
    return operation();
  probe->window(probe->context, kind, true);
  struct Finish
  {
    const IoCodecProbe &probe;
    IoCodecWindow kind;
    ~Finish() { probe.window(probe.context, kind, false); }
  } finish{*probe, kind};
  return operation();
}

struct IoDiskIndexStorage
{
  size_t folders{}, initialized{}, heights{}, columns{}, slices{}, dirty{};
  size_t tables{}, nodes{}, bucket_slots{}, requested{};
  size_t slice_slots{}, slice_requested{};
  bool supported{};
};
} // namespace cutum
