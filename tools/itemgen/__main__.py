"""CLI: python -m itemgen | python tools/itemgen.py"""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

# Allow `python tools/itemgen.py` and `python -m itemgen` from tools/.
_TOOLS = Path(__file__).resolve().parents[1]
if str(_TOOLS) not in sys.path:
    sys.path.insert(0, str(_TOOLS))

from itemgen.pipeline import build_all, build_item, list_recipe_ids  # noqa: E402


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description="Cubatarium original item mesh generator")
    ap.add_argument("command", choices=["list", "build", "build-all"])
    ap.add_argument("--id", help="item id for build")
    ap.add_argument("--staging-only", action="store_true")
    args = ap.parse_args(argv)

    if args.command == "list":
        for item_id in list_recipe_ids():
            print(item_id)
        return 0
    if args.command == "build":
        if not args.id:
            ap.error("--id required for build")
        path = build_item(args.id, publish=not args.staging_only)
        print(f"WROTE {path}")
        return 0
    done = build_all(publish=not args.staging_only)
    print(f"Done. {len(done)} item(s).")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
