#ifndef BLOCKINSPECTDIAGNOSTICS_H
#define BLOCKINSPECTDIAGNOSTICS_H

#include <filesystem>
#include <optional>

namespace cutum
{

class UGeometryEngine;
class UWorld;
struct BlockRayHit;

class UBlockInspectDiagnostics
{
public:
  static std::filesystem::path DefaultLogPath();
  static bool ClearLog();
  static int CaptureFromCrosshair(const UWorld &world, UGeometryEngine *geometries);
  static int CaptureAndAppend(const UWorld &world, const BlockRayHit &hit,
                              UGeometryEngine *geometries);
  static int GetSampleCount();
};

} // namespace cutum

#endif
