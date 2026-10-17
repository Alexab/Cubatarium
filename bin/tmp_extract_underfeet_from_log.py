import re
import sys


def main():
    if len(sys.argv) != 2:
        print("usage: logpath")
        return 2
    path = sys.argv[1]
    pat = re.compile(r"\[Perf\] underfeet_diag")
    hits = []
    with open(path, "r", encoding="utf-8", errors="ignore") as f:
        for ln in f:
            if pat.search(ln):
                hits.append(ln.strip())
    print("hits", len(hits))
    for ln in hits[-12:]:
        print(ln)


if __name__ == "__main__":
    raise SystemExit(main())

