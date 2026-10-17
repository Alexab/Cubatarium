import json
import os
import glob


def latest_perf():
    logs_dir = r"E:\Work\Home\Cubatarium\bin\logs"
    files = glob.glob(os.path.join(logs_dir, "perf_*.jsonl"))
    if not files:
        return None
    return sorted(files, key=lambda p: os.path.getmtime(p), reverse=True)[0]


def main():
    path = latest_perf()
    if not path:
        print("no perf files")
        raise SystemExit(2)

    bad_uf_reason7 = 0
    bad_uf_mesh0 = 0
    uf_need1_mesh0 = 0
    total_spikes = 0
    with open(path, "r", encoding="utf-8", errors="ignore") as f:
        for ln in f:
            ln = ln.strip()
            if not ln:
                continue
            o = json.loads(ln)
            if o.get("kind") != "spike":
                continue
            total_spikes += 1
            need = o.get("underfeet_need", 0)
            reason = o.get("underfeet_reason", 0)
            has_mesh = o.get("underfeet_has_mesh", 0)
            if reason == 7:
                bad_uf_reason7 += 1
            if need == 1 and has_mesh == 0:
                uf_need1_mesh0 += 1
            if has_mesh == 0 and need == 1:
                bad_uf_mesh0 += 1

    print("latest:", os.path.basename(path))
    print("spikes:", total_spikes)
    print("underfeet_reason==7 spikes:", bad_uf_reason7)
    print("underfeet_need==1 & has_mesh==0 spikes:", uf_need1_mesh0)


if __name__ == "__main__":
    raise SystemExit(main())

