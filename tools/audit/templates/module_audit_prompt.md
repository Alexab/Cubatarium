Full Repository Path: E:/Work/Home/Cubatarium
Module: {module_id}
Paths: {paths}

Read:
- audit/baseline.json
- audit/dead_code.json
- audit/duplicates.json
- audit/scan_summary.json

Tasks:
1. Validate auto-scan hits in this module (confirm/reject with evidence)
2. Find manual issues: architecture, perf, style
3. Cross-check docs/TECH_DEBT_*.md open items relevant to this module

Output: audit/modules/{module_id}.json — JSON object with keys:
- module_id
- generated_at (ISO8601)
- findings: array of finding objects matching tools/audit/schema.py

Do NOT modify source code.

Priority rules:
- P0 = 0 refs + low risk
- P1 = module-local fix
- P2 = multi-file refactor
- P3 = deferred / tech debt backlog
