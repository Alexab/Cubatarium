#include "Render/Mesh/GpuFluidColumnScan.h"

#include <cstdlib>
#include <iostream>
#include <vector>

namespace
{

int gFails = 0;

void Expect(bool cond, const char *msg)
{
  if (!cond)
  {
    std::cerr << "FAIL: " << msg << "\n";
    ++gFails;
  }
}

} // namespace

int main()
{
  using namespace cutum;
  constexpr int kHeight = 8;
  const int n = CHUNK_SIZE;
  std::vector<uint8_t> flags(static_cast<size_t>(kHeight * n * n), 0);
  flags[static_cast<size_t>((2 * n + 0) * n + 0)] = 1;
  flags[static_cast<size_t>((5 * n + 0) * n + 0)] = 1;
  flags[static_cast<size_t>((1 * n + 0) * n + 1)] = 1;

  std::vector<int16_t> tops;
  ScanFluidColumnsCpu(flags.data(), kHeight, tops);
  Expect(tops[0] == 5, "col0 top=5");
  Expect(tops[1] == 1, "col1 top=1");
  Expect(tops[static_cast<size_t>(n)] == -1, "empty col top=-1");

  if (gFails != 0)
  {
    std::cerr << "gpu_fluid_column_scan_test: " << gFails << " failures\n";
    return 1;
  }
  std::cout << "gpu_fluid_column_scan_test: ok\n";
  return 0;
}
