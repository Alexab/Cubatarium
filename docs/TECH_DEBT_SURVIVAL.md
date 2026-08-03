# Tech debt: Survival difficulty

> Review at end of survival-difficulty wave. Close items when implemented or
> explicitly wont-fix.

## Open

| ID | Added in | Item | Why deferred | Target |
|----|----------|------|--------------|--------|
| TD-SURV-001 | surv-diff-v1 | Hard / Hardcore difficulty | Not requested in v1 | backlog |
| TD-SURV-002 | surv-diff-v1 | Breath drain only when eyes/head submerged | Current AABB body / `inWater`; UX+physics change | backlog |
| TD-SURV-003 | surv-diff-v1 | Peaceful: disable hostile aggro/combat | Needs-first; little hostile coverage yet | backlog |
| TD-SURV-004 | surv-diff-v1 | Difficulty in World Settings mid-session UI | `/difficulty` covers change for v1 | backlog |
| TD-SURV-005 | surv-diff-v1 | Separate starve vs drown damage rates | Shared `kStarveDamagePerSec` today | backlog |

## Closed

| ID | Closed in | Resolution |
|----|-----------|------------|
| | | |

## Notes

- Mode (`WorldGameMode`) and difficulty (`WorldDifficulty`) stay separate.
- Creative ignores difficulty (vitals frozen).
- Missing `difficulty` in `world_data.json` → `normal` for Survival worlds.
