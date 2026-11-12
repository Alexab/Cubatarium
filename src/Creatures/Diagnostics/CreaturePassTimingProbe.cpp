#include "Render/GlIncludes.h"
#include "Creatures/Diagnostics/CreaturePassTimingProbe.h"
#include <nlohmann/json.hpp>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace cutum
{
CreaturePassTimingProbe *CreaturePassTimingProbe::WindowFrameProbe = nullptr;
CreaturePassTimingProbe *CreaturePassTimingProbe::SessionProbe = nullptr;

CreaturePassTimingProbe::CreaturePassTimingProbe()
{
  const char *path = std::getenv("CUBA_CREATURE_PASS_TIMING_PATH");
  if (!path || !*path) return;
  Path = path;
  if (std::filesystem::exists(Path)) throw std::runtime_error("Creature timing output must be fresh");
  Frames.reserve(16384); // Identical bounded observer allocation in all runs.
}

void CreaturePassTimingProbe::Poll(QueryPair &pair)
{
#if !defined(__ANDROID__) && !defined(CUBATARIUM_GLES)
  if (!pair.Pending) return;
  GLint available = GL_FALSE;
  glGetQueryObjectiv(pair.End, GL_QUERY_RESULT_AVAILABLE, &available);
  if (!available) return;
  GLuint64 begin = 0, end = 0;
  glGetQueryObjectui64v(pair.Begin, GL_QUERY_RESULT, &begin);
  glGetQueryObjectui64v(pair.End, GL_QUERY_RESULT, &end);
  auto &frame = Frames[pair.FrameIndex];
  if (end >= begin) { frame.GpuNs = end - begin; frame.GpuReady = true; }
  pair.Pending = false;
#endif
}

void CreaturePassTimingProbe::BeginFrame(float dt, float x, float y, float z)
{
  if (!Enabled()) return;
  if (!Initialized)
  {
#if !defined(__ANDROID__) && !defined(CUBATARIUM_GLES)
    GpuSupported = (GLEW_VERSION_3_3 || GLEW_ARB_timer_query) && glQueryCounter && glGetQueryObjectui64v;
    GLint counterBits = 0;
    if (GpuSupported) glGetQueryiv(GL_TIMESTAMP, GL_QUERY_COUNTER_BITS, &counterBits);
    GpuSupported = GpuSupported && counterBits > 0;
    if (GpuSupported) for (auto &pair : Queries)
    { glGenQueries(1, &pair.Begin); glGenQueries(1, &pair.End); }
#endif
    Initialized = true;
  }
  if (Frames.size() == Frames.capacity()) { ++DroppedFrames; FrameActive = false; return; }
  Current = Frames.size(); Frames.emplace_back();
  auto &frame = Frames.back(); frame.Dt = dt; frame.Camera = {x,y,z};
  Slot = Current % Queries.size();
  if (GpuSupported) Poll(Queries[Slot]);
  FrameActive = true; WindowFrameProbe = this; SessionProbe = this;
  Start = Clock::now(); // Excludes diagnostic allocation/poll above.
}

void CreaturePassTimingProbe::RecordPose(LocomotionState state, double ms)
{
  if (!FrameActive) return;
  auto &frame = Frames[Current]; frame.PoseMs += ms;
  const size_t index = static_cast<size_t>(state);
  if (index < frame.States.size()) ++frame.States[index];
}

void CreaturePassTimingProbe::BeginGpuDraw()
{
#if !defined(__ANDROID__) && !defined(CUBATARIUM_GLES)
  if (!FrameActive || !GpuSupported) return;
  auto &pair = Queries[Slot];
  if (pair.Pending) { ++SkippedGpuQueries; return; }
  pair.FrameIndex = Current;
  glQueryCounter(pair.Begin, GL_TIMESTAMP); QueryActive = true;
#endif
}

void CreaturePassTimingProbe::EndGpuDraw()
{
#if !defined(__ANDROID__) && !defined(CUBATARIUM_GLES)
  if (!QueryActive) return;
  auto &pair = Queries[Slot];
  glQueryCounter(pair.End, GL_TIMESTAMP); pair.Pending = true; QueryActive = false;
#endif
}

void CreaturePassTimingProbe::EndFrame(const CreatureRenderStats &stats)
{
  if (!FrameActive) return;
  Frames[Current].PassMs = std::chrono::duration<double,std::milli>(Clock::now()-Start).count();
  Frames[Current].Stats = stats; FrameActive = false;
}

void CreaturePassTimingProbe::CompleteWindowFrame(double wallMs, double swapMs)
{
  if (!WindowFrameProbe) return;
  auto &frame = WindowFrameProbe->Frames[WindowFrameProbe->Current];
  frame.WallMs = wallMs; frame.SwapMs = swapMs;
  WindowFrameProbe = nullptr;
}

void CreaturePassTimingProbe::ShutdownSession()
{
  if (SessionProbe) SessionProbe->Shutdown();
}

void CreaturePassTimingProbe::Shutdown()
{
  if (!Enabled()) return;
  // InitBuffers calls DestroyBuffers before creating the first GL buffers.
  if (!Initialized && Frames.empty()) return;
  if (WindowFrameProbe == this) WindowFrameProbe = nullptr;
  if (SessionProbe == this) SessionProbe = nullptr;
  for (auto &pair : Queries)
  {
    if (GpuSupported) Poll(pair);
#if !defined(__ANDROID__) && !defined(CUBATARIUM_GLES)
    if (pair.Begin) glDeleteQueries(1, &pair.Begin);
    if (pair.End) glDeleteQueries(1, &pair.End);
#endif
    pair = {};
  }
  using Json = nlohmann::json;
  std::ofstream out(Path);
  out << Json({{"kind","schema"},{"schema","creature-pass-timing.v1"},
    {"scope","Individual actual world frames; CPU pose/pass, GPU Queue.Flush command interval; opt-in observer, no blocking GPU reads"},
    {"gpu_supported",GpuSupported},{"observer_reserved_frame_bytes",Frames.capacity()*sizeof(Frame)}}).dump() << '\n';
  for (size_t i=0;i<Frames.size();++i)
  {
    const auto &f = Frames[i]; Json states = Json::object();
    for (size_t s=0;s<f.States.size();++s) if (f.States[s])
      states[ToString(static_cast<LocomotionState>(s))] = f.States[s];
    out << Json({{"kind","frame"},{"frame",i},{"dt_seconds",f.Dt},{"camera",f.Camera},
      {"cpu_pass_ms",f.PassMs},{"cpu_pose_ms",f.PoseMs},{"window_wall_ms",f.WallMs},{"swap_ms",f.SwapMs},
      {"gpu_draw_interval_ns",f.GpuReady?Json(f.GpuNs):Json(nullptr)},
      {"considered",f.Stats.CreaturesConsidered},{"drawn",f.Stats.CreaturesDrawn},
      {"culled",f.Stats.CreaturesCulled},{"draw_calls",f.Stats.CreatureDrawCalls},
      {"palette_uploads",f.Stats.CreatureBoneMatrixUploads},{"pose_states",states}}).dump() << '\n';
  }
  out << Json({{"kind","end"},{"frames",Frames.size()},{"dropped_frames",DroppedFrames},
    {"skipped_gpu_queries",SkippedGpuQueries}}).dump() << '\n';
  out.flush();
  if (!out) std::cerr << "Creature timing output write failed: " << Path << '\n';
  Path.clear();
}
}
