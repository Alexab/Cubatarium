import json
from pathlib import Path
from collections import Counter

p = Path("bin/logs/enter_lit_20260827-102235.jsonl")
rows = [json.loads(l) for l in p.open(encoding="utf-8") if l.strip()]
print("terminal held", Counter(r.get("enter_terminal_held_n") for r in rows).most_common(8))
print("phantom pruned", Counter(r.get("enter_phantom_dirty_pruned_n") for r in rows).most_common(5))
print("gate_done", Counter(r.get("gate_done_n") for r in rows).most_common(8))
print("visibility_debt", Counter(r.get("visibility_debt") for r in rows).most_common(8))
print("mesh_dirty flag", Counter(r.get("mesh_dirty") for r in rows))
# when dn=0 miss=1: top miss witness
print("top miss when dn0", Counter((r.get("top_dirty_cx"), r.get("top_dirty_cz")) for r in rows if r.get("dirty_n")==0).most_common(10))
