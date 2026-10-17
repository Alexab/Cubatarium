import re


def main():
    path = r"E:\Work\Home\Cubatarium\bin\logs\Cubatarium.exe.TIMLENOVO.Bakhshiev.log.INFO.20260818-113413.15312"
    try:
        with open(path, "r", encoding="utf-8", errors="ignore") as f:
            lines = f.readlines()
    except FileNotFoundError:
        print("missing:", path)
        return 2

    pat = re.compile(r"\[Perf\] underfeet_diag")
    hits = [ln.strip() for ln in lines if pat.search(ln)]
    print("hits", len(hits))
    for ln in hits[-10:]:
        print(ln)


if __name__ == "__main__":
    raise SystemExit(main())

