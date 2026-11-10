#ifndef ISOVIEWPRESET_H
#define ISOVIEWPRESET_H

#include <cstdint>

namespace cutum
{

enum class IsoViewPreset : uint8_t
{
  Close = 0,
  Standard = 1,
  Far = 2,
};

inline IsoViewPreset CycleIsoViewPreset(IsoViewPreset preset)
{
  switch (preset)
  {
  case IsoViewPreset::Close:
    return IsoViewPreset::Standard;
  case IsoViewPreset::Standard:
    return IsoViewPreset::Far;
  case IsoViewPreset::Far:
  default:
    return IsoViewPreset::Close;
  }
}

inline const char *IsoViewPresetLabel(IsoViewPreset preset)
{
  switch (preset)
  {
  case IsoViewPreset::Close:
    return "Iso Close";
  case IsoViewPreset::Standard:
    return "Iso Standard";
  case IsoViewPreset::Far:
    return "Iso Far";
  }
  return "Iso";
}

inline float IsoBoomDistanceForPreset(IsoViewPreset preset)
{
  switch (preset)
  {
  case IsoViewPreset::Close:
    return 10.0f;
  case IsoViewPreset::Far:
    return 36.0f;
  case IsoViewPreset::Standard:
  default:
    return 20.0f;
  }
}

} // namespace cutum

#endif
