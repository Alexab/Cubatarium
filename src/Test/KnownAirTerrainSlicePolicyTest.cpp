#include "World/Chunks/KnownAirTerrainSlicePolicy.h"
#include <cstdlib>
#include <iostream>

using namespace cutum;
void Expect(bool value, const char *why)
{
  if (!value) { std::cerr << why << '\n'; std::exit(1); }
}
int main()
{
  // Ocean surface at block63, body centre crossing into slice4: the completed
  // column stores water/terrain through slice3 and deliberately omits sky4.
  KnownAirTerrainSliceEvidence ocean{4, 0, 15, true, true, false, 3, 3};
  Expect(IsKnownAirTerrainSlice(ocean), "completed ocean sky is known air");
  auto unknown = ocean; unknown.columnComplete = false;
  Expect(!IsKnownAirTerrainSlice(unknown), "an emerging column cannot prove sky");
  auto pending = ocean; pending.savePending = true;
  Expect(!IsKnownAirTerrainSlice(pending), "pending persistence cannot prove missing data is air");
  auto unsaved = ocean; unsaved.hasWorldFolder = false;
  Expect(!IsKnownAirTerrainSlice(unsaved), "missing storage identity stays conservative");
  auto diskTower = ocean; diskTower.highestOnDiskY = 8;
  Expect(!IsKnownAirTerrainSlice(diskTower), "unloaded saved tower must not be classified as sky");
  auto residentTower = ocean; residentTower.highestResidentY = 4;
  Expect(!IsKnownAirTerrainSlice(residentTower), "hole below or at resident data is unknown");
  auto undergroundHole = ocean; undergroundHole.sliceY = 2;
  Expect(!IsKnownAirTerrainSlice(undergroundHole), "missing underground slice is not empty sky");
  auto negativeWorld = ocean; negativeWorld.minSliceY = -4;
  negativeWorld.sliceY = -1; negativeWorld.highestResidentY = -2; negativeWorld.highestOnDiskY = -2;
  Expect(IsKnownAirTerrainSlice(negativeWorld), "negative height worlds use slice ordering");
  auto outside = unknown; outside.sliceY = 16;
  Expect(IsKnownAirTerrainSlice(outside), "ray trace retains outside-world air policy");
  std::cout << "Known sky versus emerging/saved/internal missing terrain: PASS\n";
}
