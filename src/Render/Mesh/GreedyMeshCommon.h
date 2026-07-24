#pragma once

namespace cutum
{

inline int FaceIndexFromGreedy(int axis, int faceSign)
{
  if (axis == 2)
  {
    return faceSign > 0 ? 0 : 2;
  }
  if (axis == 0)
  {
    return faceSign > 0 ? 1 : 3;
  }
  return faceSign > 0 ? 4 : 5;
}

} // namespace cutum
