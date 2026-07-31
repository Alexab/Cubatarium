#pragma once

#include "Render/Backend/RenderBackendCaps.h"
#include <string>
#include <vector>

namespace cutum
{

struct AndroidGpuAllowlistConfig
{
  bool AllowlistEnabled{true};
  std::string MinGles{"3.1"};
  std::vector<std::string> AllowRenderers{"Adreno (TM)", "Mali-G"};
};

/// Load allowlist from path; on failure returns defaults with AllowlistEnabled=true.
AndroidGpuAllowlistConfig LoadAndroidGpuAllowlist(const char *path);

bool MatchAndroidGpuAllowlist(const RenderBackendCaps &caps,
                              const AndroidGpuAllowlistConfig &cfg);

/// Sets caps.AllowAndroidGpu and caps.AndroidGpuDenyReason.
/// android_gpu_enabled defaults to true (GPU-by-default); false = user opt-out.
void ApplyAndroidGpuPolicy(RenderBackendCaps &caps, bool android_gpu_enabled,
                           const AndroidGpuAllowlistConfig *allowlist = nullptr);

/// Env CUBATARIUM_ANDROID_GPU=0 force CPU; =1 force attempt (skip allowlist).
void ApplyAndroidGpuEnvOverrides(RenderBackendCaps &caps,
                                 bool &android_gpu_enabled);

} // namespace cutum
