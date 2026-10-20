#ifndef MESHAUDITDIAGNOSTICS_H
#define MESHAUDITDIAGNOSTICS_H

#include <filesystem>

namespace cutum
{

class UWorld;

/// Oracle for see-through holes: for every resident chunk, compare the
/// brute-force set of exposed opaque top faces in the live voxel data with the
/// top faces produced by a fresh snapshot + mesher run, and record the cache
/// state of chunks that disagree (or whose cached mesh is not drawable).
/// Writes <exe_dir>/mesh_audit.json. Triggered by CUBA_MESH_AUDIT_AFTER_SEC.
class UMeshAuditDiagnostics
{
public:
  static std::filesystem::path DefaultPath();
  /// Returns number of chunks audited, or -1 on failure.
  static int Run(UWorld &world, const std::filesystem::path &path = {});
};

} // namespace cutum

#endif
