import os
import glob
import json
from datetime import datetime


def main():
    logs_dir = r"E:\Work\Home\Cubatarium\bin\logs"
    files = glob.glob(os.path.join(logs_dir, "perf_*.jsonl"))
    files = sorted(files, key=lambda p: os.path.getmtime(p), reverse=True)
    print("count", len(files))
    for p in files[:30]:
        m = os.path.getmtime(p)
        # Print a compact stamp from filename too
        name = os.path.basename(p)
        print(name, "mtime", int(m))


if __name__ == "__main__":
    raise SystemExit(main())

