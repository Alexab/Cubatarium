#pragma once

#include "World/Streaming/ColumnJobGraph.h"
#include "World/Streaming/ColumnRecord.h"

#include <atomic>
#include <cstdint>
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
  /// Real GPU residency token when mesh service exposes one; else 0.
  uint64_t published_gpu_handle{0};
};

/// Q6 cutover stages. ShadowCompare never runs two expensive jobs.
enum class ColumnCutoverStage : uint8_t
{
  ShadowCompare = 0, ///< Log mismatch; legacy still owns enqueue.
  FirstMeshOwner = 1, ///< RecordCoordinator sole FirstMesh Enqueue/Cancel owner.
  RelightOwner = 2,
  SeamOwner = 3,
  EvictionOwner = 4,
};

/// M11: scheduling-state coordinator (shadow-friendly; no duplicate jobs).
class UColumnRecordCoordinator
{
public:
  static ColumnCutoverStage GetCutoverStage();
  /// Rollback: set ShadowCompare to restore legacy sole ownership.
  static void SetCutoverStage(ColumnCutoverStage stage);

  static uint64_t ShadowMismatchCount();
  static void ResetShadowMismatchCount();

  /// Dual-write published/pending/resident from world scan; returns record stage.
  static ColumnJobStage SyncFromWorldTruth(ColumnRecord &rec,
                                            const ColumnWorldTruth &truth);

  /// Derive scheduler stage from record (published + pending independent).
  static ColumnJobStage DeriveJobStageFromRecord(const ColumnRecord &rec);

  /// Shadow mode: log when legacy monolithic derive disagrees with record path.
  static void LogShadowMismatch(glm::ivec2 column, ColumnJobStage legacy_stage,
                                ColumnJobStage record_stage);

  /// FirstMesh enqueue decision. In ShadowCompare: returns legacy_want and
  /// logs when record_want differs (no dual job). In FirstMeshOwner+: record_want
  /// is authoritative for FirstMesh.
  static bool DecideFirstMeshEnqueue(bool legacy_want, bool record_want,
                                     glm::ivec2 column = {});

  /// Relight enqueue decision (RelightThenMesh / PromoteRelight). Same shadow
  /// contract; authoritative when stage >= RelightOwner.
  static bool DecideRelightEnqueue(bool legacy_want, bool record_want,
                                   glm::ivec2 column = {});
};

} // namespace cutum
