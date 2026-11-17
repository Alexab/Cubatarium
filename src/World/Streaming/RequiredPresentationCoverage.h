#pragma once
#include <cstdint>

namespace cutum
{
// Census of the complete configured ring and vertical profile, not only the
// resident/drawable list. AIR receipts prove geometry absence, not light bytes.
struct RequiredPresentationCoverage
{
  int radius{0};
  int min_y{0}, max_y{-1};
  int columns{0}, expected_columns{0};
  int sections{0}, expected_sections{0};
  int missing_data_columns{0};
  int unknown_sections{0};
  int known_air_sections{0};
  int resident_air_sections{0};
  int surface_sections{0};
  int light_unready_sections{0};
  int publication_unready_sections{0};

  void ObserveAbsent(bool known_air)
  {
    ++sections;
    if (known_air)
      ++known_air_sections;
    else
      ++unknown_sections;
  }
  void ObserveResident(bool air, bool light_current, bool publication_current)
  {
    ++sections;
    if (air)
      ++resident_air_sections;
    else
      ++surface_sections;
    if (!light_current)
      ++light_unready_sections;
    if (!air && !publication_current)
      ++publication_unready_sections;
  }
  bool Ready() const
  {
    return expected_columns > 0 && expected_sections > 0 &&
           columns == expected_columns && sections == expected_sections &&
           known_air_sections + resident_air_sections + surface_sections +
                   unknown_sections ==
               sections &&
           missing_data_columns == 0 && unknown_sections == 0 &&
           light_unready_sections == 0 && publication_unready_sections == 0;
  }
};
} // namespace cutum
