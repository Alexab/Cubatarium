#ifndef CREATURE_TICK_TRACE_H
#define CREATURE_TICK_TRACE_H

namespace cutum
{

class UWorld;

/// Environment-driven creature tick diagnostics, called once per NPC tick
/// before the creatures move (formerly inline "TEMP diag" in
/// UWorld::TickNpcCreatureIntents). Does nothing unless enabled:
/// - CUBA_QA_TICKDUMP_ID=<creatureId>: per-frame trace of that creature
///   (position, support, locomotion, facts, intent, collision, chunks) to
///   "<id>_trace.txt" for CUBA_QA_TICKDUMP_FRAMES frames (default 400).
/// - CUBA_QA_TICKDUMP=<file>: every creature's tick state at frames 60 and
///   300, appended to <file>.
void RecordCreatureTickTrace(UWorld &world, bool tickWorldAi);

} // namespace cutum

#endif
