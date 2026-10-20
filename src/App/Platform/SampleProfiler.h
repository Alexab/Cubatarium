#pragma once

namespace cutum
{

/// Opt-in main-thread sampling profiler (Windows only, no-op elsewhere).
/// Enabled with CUBA_SAMPLE_PROFILE=1; samples every
/// CUBA_SAMPLE_PROFILE_INTERVAL_MS (default 2 ms) with an allocation-free
/// RtlVirtualUnwind and writes <exe_dir>/sample_profile.txt at process exit
/// (top inclusive / self functions). Used to find hot spots in the huge
/// streaming/meshing code without instrumenting it.
void StartSampleProfilerIfRequested();
/// Stop sampling and write the report now (idempotent; also runs at exit).
void FinishSampleProfiler();

} // namespace cutum
