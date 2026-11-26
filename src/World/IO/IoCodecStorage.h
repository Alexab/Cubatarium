#pragma once

#include <cstddef>

namespace cutum
{
enum class IoCodecKind
{
  BinaryWrite, BinaryRead, JsonWrite, JsonRead, ReceiptEncode, ReceiptWrite, ReceiptRead, FlagsWrite, Count
};

struct IoCodecStorage
{
  IoCodecKind kind{IoCodecKind::Count};
  size_t scratch_bytes{}, input_bytes{}, output_bytes{}, stack_buffer_bytes{};
  bool supported{};
};

// Called synchronously while the named codec buffers are alive. The sample
// contains counts only and may be copied after the codec returns.
struct IoCodecProbe
{
  void *context;
  void (*sample)(void *, const IoCodecStorage &) noexcept;
};

struct IoDiskIndexStorage
{
  size_t folders{}, initialized{}, heights{}, columns{}, slices{}, dirty{};
  size_t tables{}, nodes{}, bucket_slots{}, requested{};
  bool supported{};
};
} // namespace cutum
