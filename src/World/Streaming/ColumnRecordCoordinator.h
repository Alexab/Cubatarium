#pragma once

#include "World/Streaming/ColumnJobGraph.h"
#include "World/Streaming/ColumnRecord.h"

#include <glm/glm.hpp>

namespace cutum
{

/// World/mesh scan inputs for coordinator (scheduler truth, not events).
struct ColumnWorldTruth
{
  bool has_chunk{false};
  bool pending_light{false};
  bool lit_ready{false};
  bool meshing{false};
  bool gpu_pending{false};
  /// Valid published draw handle (independent of pending replacement).
  bool render_ready{false};
};

/// M11: scheduling-state coordinator (shadow-friendly; no duplicate jobs).
class UColumnRecordCoordinator
{
public:
  /// Dual-write published/pending/resident from world scan; returns record stage.
  static ColumnJobStage SyncFromWorldTruth(ColumnRecord &rec,
                                            const ColumnWorldTruth &truth);

  /// Derive scheduler stage from record (published + pending independent).
  static ColumnJobStage DeriveJobStageFromRecord(const ColumnRecord &rec);

  /// Shadow mode: log when legacy monolithic derive disagrees with record path.
  static void LogShadowMismatch(glm::ivec2 column, ColumnJobStage legacy_stage,
                                ColumnJobStage record_stage);
};

} // namespace cutum
