#pragma once

#include "Render/Backend/RenderBackendCaps.h"
#include <string>

namespace cutum
{

/// Thin wrapper around GL compute program compile/link for Desktop GL and GLES.
class UComputeProgram
{
public:
  UComputeProgram() = default;
  ~UComputeProgram();
  UComputeProgram(const UComputeProgram &) = delete;
  UComputeProgram &operator=(const UComputeProgram &) = delete;

  /// Compile from file paths chosen by caps.Platform (desktop vs gles).
  bool CompileForCaps(const RenderBackendCaps &caps,
                      const std::string &desktop_path,
                      const std::string &gles_path);

  /// Compile from inline GLSL source (already versioned for the platform).
  bool CompileSource(const char *source);

  unsigned Program() const { return ProgramId; }
  bool Valid() const { return ProgramId != 0; }
  void Destroy();

private:
  unsigned ProgramId{0};
};

} // namespace cutum
