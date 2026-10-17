import os
import glob


def main():
    logs_dir = r"E:\Work\Home\Cubatarium\bin\logs"
    pats = [
        os.path.join(logs_dir, "*.log.INFO.*"),
        os.path.join(logs_dir, "*.log.INFO*"),
        os.path.join(logs_dir, "*underfeet*"),
    ]
    files = []
    for p in pats:
        files.extend(glob.glob(p))
    files = sorted(set(files), key=lambda f: os.path.getmtime(f), reverse=True)
    print("found", len(files), "files")
    for f in files[:20]:
        print(os.path.basename(f), "mtime", os.path.getmtime(f))


if __name__ == "__main__":
    raise SystemExit(main())

