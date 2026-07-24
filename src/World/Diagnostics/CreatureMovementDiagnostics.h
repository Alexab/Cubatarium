#ifndef CREATUREMOVEMENTDIAGNOSTICS_H
#define CREATUREMOVEMENTDIAGNOSTICS_H

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>

#include <glm/glm.hpp>

namespace cutum
{

struct CreatureMovementDiagRecord
{
  std::string event;
  uint64_t creatureId{0};
  std::string typeId;
  std::string habitat;
  std::string behavior;
  std::string fsmState;
  std::string reason;
  std::string goalSource;
  glm::vec3 body{0.0f};
  glm::vec3 intentDir{0.0f};
  float intentSpeed{0.0f};
  glm::vec3 travel{0.0f};
  bool onGround{false};
  bool inFluid{false};
  bool pathValid{false};
  int waypointCount{-1};
  bool activityTick{false};
};

class UCreatureMovementDiagnostics
{
public:
  static void SetEnabled(bool enabled);
  static bool IsEnabled();

  /// 0 = record all creatures when enabled.
  static void SetFocusId(uint64_t creature_id);
  static uint64_t GetFocusId();

  static void SetLogPathOverride(const std::filesystem::path &path);
  static void ClearLogPathOverride();
  static std::filesystem::path DefaultLogPath();

  static bool ClearLog();
  static int GetSampleCount();
  static int GetRingSize();

  /// Appends JSONL when enabled and focus matches. Returns sample index or -1.
  static int Record(const CreatureMovementDiagRecord &record);

  /// Writes in-memory ring to a dump file (default: creature_movement_diag.dump.jsonl).
  static bool DumpRing(const std::filesystem::path &path = {});
};

} // namespace cutum

#endif
