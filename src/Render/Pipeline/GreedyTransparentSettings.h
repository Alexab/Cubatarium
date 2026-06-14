#ifndef GREEDY_TRANSPARENT_SETTINGS_H
#define GREEDY_TRANSPARENT_SETTINGS_H

namespace cutum
{

struct GreedyTransparentSettings
{
  /// Texels with alpha >= shellAlpha form the depth shell (default 0.35).
  float shellAlpha = 0.35f;
  bool logPassNames = false;
};

} // namespace cutum

#endif
