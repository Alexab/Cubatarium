#pragma once

#include <cstddef>

namespace cutum
{
// The consumer borrows the encoded PNG until the call returns.
using InventoryIconPngConsumer = bool (*)(void *, const unsigned char *, std::size_t, std::size_t);
bool EncodeInventoryIconPng(const unsigned char *rgba, int size, void *context, InventoryIconPngConsumer consume);
} // namespace cutum
