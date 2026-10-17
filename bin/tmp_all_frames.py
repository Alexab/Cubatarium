import json

f = r"E:\Work\Home\Cubatarium\bin\logs\perf_20260818-105023_28028.jsonl"
lines = [json.loads(l) for l in open(f)]
for i, s in enumerate(lines):
    if s.get("kind") == "shutdown":
        continue
    print(f"#{i} kind={s['kind']} wall={s['wall_ms']:.0f} phys={s['phys_ms']:.0f}"
          f" do_move={s.get('do_movement_ms',0):.0f}"
          f" stream={s.get('stream_ms',0):.0f}"
          f" emerge={s.get('mesh_emerge_ms',0):.0f}"
          f" imm_ms={s.get('mesh_immediate_ms',0):.0f}"
          f" dirty_tick={s.get('mesh_dirty_tick_ms',0):.0f}"
          f" uf_reason={s.get('underfeet_reason','?')}"
          f" uf_need={s.get('underfeet_need','?')}"
          f" apply_n={s.get('relight_apply_n','?')}"
          f" pend_light={s.get('pending_light','?')}")
