"""CLI: python -m itemgen | python tools/itemgen.py"""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

# Allow `python tools/itemgen.py` and `python -m itemgen` from tools/.
_TOOLS = Path(__file__).resolve().parents[1]
if str(_TOOLS) not in sys.path:
    sys.path.insert(0, str(_TOOLS))

from itemgen.pipeline import build_all, build_item, list_recipe_ids, validate_item, publish_item  # noqa: E402
from itemgen.preview import make_contact_sheet, make_family_sheet  # noqa: E402
from itemgen.quality import MATERIALS, FAMILIES, run_audit  # noqa: E402


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description="Cubatarium original item mesh generator")
    ap.add_argument(
        "command",
        choices=["list", "build", "build-design", "validate-design", "publish-design", "recover-publication", "seed-workshop", "seed-supplies", "seed-armor", "build-all", "validate", "publish", "preview", "preview-all", "audit"],
    )
    ap.add_argument("--id", help="item id for build/preview")
    ap.add_argument("--family", help="family for preview sheet")
    ap.add_argument("--staging-only", action="store_true")
    ap.add_argument("--all", action="store_true",help="Select all authored designs for versioned validation/publication")
    args = ap.parse_args(argv)

    if args.command == 'recover-publication':
        from itemgen.design_publication import recover_publication
        print(recover_publication())
        return 0
    if args.command in ('validate-design','publish-design'):
        from itemgen.design_publication import validate_design,publish_designs
        if bool(args.id)==bool(args.all):ap.error('Choose --id or --all')
        ids=[args.id] if args.id else sorted(p.parent.name for p in (_TOOLS/'item_designs').glob('*/design.yaml'))
        if args.command=='publish-design':print(publish_designs(ids))
        else:
            for id in ids:
                result=validate_design(id)
                print(f"VALIDATED {id}: {result['triangles']} triangles")
        return 0

    if args.command == "seed-workshop":
        from itemgen.workshop_designs import seed_missing
        print(f"Created {seed_missing(_TOOLS.parent)}")
        return 0

    if args.command == "seed-supplies":
        from itemgen.supplies_designs import seed_missing
        print(f"Created {seed_missing(_TOOLS.parent)}")
        return 0

    if args.command == "seed-armor":
        from itemgen.armor_designs import seed_missing
        print(f"Created {seed_missing(_TOOLS.parent)}")
        return 0

    if args.command == "build-design":
        if not args.id:
            ap.error("--id required for build-design")
        from itemgen.design import build_design
        print(f"STAGED {build_design(args.id)}")
        return 0

    if args.command == "list":
        for item_id in list_recipe_ids():
            print(item_id)
        return 0
    if args.command in ("validate","publish"):
        if not args.id:ap.error("--id required")
        print(validate_item(args.id) if args.command=="validate" else publish_item(args.id))
        return 0
    if args.command == "build":
        if not args.id:
            ap.error("--id required for build")
        path = build_item(args.id, publish=False)
        print(f"WROTE {path}")
        return 0
    if args.command == "build-all":
        done = build_all(publish=False)
        print(f"Done. {len(done)} item(s).")
        return 0
    if args.command == "preview":
        if args.family:
            path = make_family_sheet(args.family, MATERIALS)
            print(f"WROTE {path}")
            return 0
        if not args.id:
            ap.error("--id or --family required for preview")
        path = make_contact_sheet(args.id)
        print(f"WROTE {path}")
        return 0
    if args.command == "preview-all":
        for item_id in list_recipe_ids():
            print(make_contact_sheet(item_id))
        for family in FAMILIES:
            print(make_family_sheet(family, MATERIALS))
        return 0
    findings = run_audit()
    print(f"audit passed={findings['passed']} failures={len(findings['failures'])}")
    for fail in findings["failures"]:
        print(f"FAIL {fail}")
    return 0 if findings["passed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
