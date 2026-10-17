from pathlib import Path
p = Path("src/World/Streaming/ChunkEmergeCoordinator.cpp")
t = p.read_text(encoding="utf-8")
# Normalize to \n for editing
t = t.replace("\r\n", "\n")
marker = "mesh_service.SetPendingLightFocusPressure(pending_focus_count);\n  if (phase_abort_heavy)\n  {\n    // Reinforce AbortDripCap"
idx = t.find(marker)
print("marker idx", idx)
if idx < 0:
    raise SystemExit(1)
# find end of this if block via LastBudget.MaxMeshDrain after reinforce
end_marker = "    LastBudget.MaxMeshDrain = mesh_drain;\n  }\n  mesh_service.SetVisibleBlackNoTicketPressure"
end = t.find(end_marker, idx)
print("end idx", end)
if end < 0:
    raise SystemExit(2)
end += len("    LastBudget.MaxMeshDrain = mesh_drain;\n  }\n")
new_block = """mesh_service.SetPendingLightFocusPressure(pending_focus_count);
  // Phase 5.4.2 MissWitnessScheduleFloor: dirty_fm starved -> schedule>=1 before
  // AbortDripN reinforce (keeps PreferKick / SoftDefer intent alive).
  if (world.GetPhysicsTelemetry().FocusMissingMesh > 0 &&
      mesh_service.GetLastDirtyFmN() > 0 &&
      mesh_service.GetLastMeshDirtyScheduleOkN() == 0)
  {
    mesh_schedule = std::max(mesh_schedule, 1);
  }
  if (phase_abort_heavy)
  {
    // Reinforce AbortDripCap after late floors (calm/alpha must not starve drip).
    if (abort_needs_drip)
    {
      const int drip =
          AbortDripN(world.GetPhysicsTelemetry(), nearest_miss_h,
                     MissWitnessAgeFrames, missing_underfeet);
      mesh_schedule = std::min(std::max(mesh_schedule, 1), drip);
      mesh_drain = std::min(std::max(mesh_drain, 1), drip);
    }
    else
    {
      mesh_schedule = 0;
      mesh_drain = 0;
    }
    LastBudget.MaxMeshSchedule = mesh_schedule;
    LastBudget.MaxMeshDrain = mesh_drain;
  }
"""
# Extract comment word from file for accuracy
snippet = t[idx:end]
print("OLD SNIPPET START:", repr(snippet[:200]))
t2 = t[:idx] + new_block + t[end:]
p.write_bytes(t2.replace("\n", "\r\n").encode("utf-8"))
print("OK replaced", end - idx, "bytes with", len(new_block))
