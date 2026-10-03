import json


def main():
    path = r"E:\Work\Home\Cubatarium\bin\logs\perf_20260818-113418_15312.jsonl"
    objs = []
    with open(path, "r", encoding="utf-8", errors="ignore") as f:
        for ln in f:
            ln = ln.strip()
            if not ln:
                continue
            objs.append(json.loads(ln))
    print("lines", len(objs))
    for o in objs:
        if o.get("kind") not in ("spike", "period"):
            continue
        print(
            o.get("kind"),
            "wall",
            o.get("wall_ms"),
            "underfeet_reason",
            o.get("underfeet_reason"),
            "stage",
            o.get("underfeet_stage"),
            "need",
            o.get("underfeet_need"),
            "pending_light",
            o.get("underfeet_pending_light"),
            "draw_ok",
            o.get("underfeet_draw_ok"),
            "mesh",
            o.get("underfeet_has_mesh"),
            "opaque_cmd_on",
            o.get("opaque_cmd_on"),
            "relight_apply_n",
            o.get("relight_apply_n"),
            "pending_light_n",
            o.get("pending_light_n"),
            "relight_false_clear_n",
            o.get("relight_false_clear_n"),
            "relight_fifo_dropped",
            o.get("relight_fifo_dropped"),
        )


if __name__ == "__main__":
    raise SystemExit(main())

