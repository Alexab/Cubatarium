import json
for name in ["perf_20260818-083708_13240.jsonl", "perf_20260818-105023_28028.jsonl"]:
    f = rf"E:\Work\Home\Cubatarium\bin\logs\{name}"
    lines = [json.loads(l) for l in open(f)]
    spikes = [l for l in lines if l.get("kind") == "spike"]
    if spikes:
        s = spikes[0]
        print(f"{name}: first spike px={s.get('player_x')} py={s.get('player_y')} pz={s.get('player_z')} chunks={s.get('chunk_count')}")
        s = spikes[-1]
        print(f"  last spike px={s.get('player_x')} py={s.get('player_y')} pz={s.get('player_z')} chunks={s.get('chunk_count')}")
