#ifndef BLOCKTYPES_H
#define BLOCKTYPES_H

#include <cstdint>

namespace cutum
{

using BlockId = uint16_t;

constexpr BlockId BLOCK_AIR = 0;

constexpr BlockId kPackBlockIdMin = 1;
constexpr BlockId kPackBlockIdMax = 4095;
constexpr BlockId kRuntimeBlockIdMin = 4096;

} // namespace cutum

#endif
