#pragma once

namespace cutum
{

class UWorld;

struct EnterLitSample
{
  double elapsed_ms{0.0};
  int snapshot_debt{0};
  int snapshot_size{0};
  int pending_global{0};
  int fifo_n{0};
  int inflight{0};
  int chunk_resident{0};
  bool streaming_frozen{false};
};

class UEnterLitDiagnostics
{
public:
  static void BeginSession();
  static void EndSession();
  static void Sample(UWorld &world, double elapsed_ms, EnterLitSample &out);
  static void MaybeLog(const EnterLitSample &sample, int frame_index,
                       int every_n_frames = 30);
};

} // namespace cutum
