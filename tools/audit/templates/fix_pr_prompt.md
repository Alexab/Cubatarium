Full Repository Path: E:/Work/Home/Cubatarium
PR: {pr_id}

Read audit/findings.json — implement ONLY approved finding IDs for this PR.

Rules:
- Legacy migration paths: do NOT remove Load*/Migrate*
- After changes run verification commands
- Update related docs in same PR
- Do NOT git commit unless user asks

Verification (all must pass):
  ./scripts/doctor-windows.ps1
  python tools/smoke_resource_packs.py
  python tools/integration_test_worldgen.py --exe bin/Cubatarium.exe
  python tools/audit_style.py

Mark findings done in audit/findings.json with "implemented_in": "{pr_id}".
