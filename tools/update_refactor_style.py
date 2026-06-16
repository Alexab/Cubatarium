#!/usr/bin/env python3
"""Merge generated member renames into refactor_style.py."""

import re
from pathlib import Path

TOOLS = Path(__file__).resolve().parent
STYLE = TOOLS / "refactor_style.py"
MEMBERS_OUT = TOOLS / "_members_out.txt"

PLATFORM_CLASSES = [
    ("AndroidPlatformWindow", "UAndroidPlatformWindow"),
    ("AndroidPlatformPaths", "UAndroidPlatformPaths"),
    ("DesktopPlatformWindow", "UDesktopPlatformWindow"),
    ("DesktopPlatformPaths", "UDesktopPlatformPaths"),
    ("TouchInputBridge", "UTouchInputBridge"),
    ("GuiTouchControls", "UGuiTouchControls"),
    ("TouchControlPanel", "UTouchControlPanel"),
    ("TouchHoldButton", "UTouchHoldButton"),
    ("TouchVirtualJoystick", "UTouchVirtualJoystick"),
    ("TouchLookPad", "UTouchLookPad"),
    ("GlfwClipboard", "UGlfwClipboard"),
    ("NullClipboard", "UNullClipboard"),
    ("AssetStreamBuf", "UAssetStreamBuf"),
]


def parse_members(text: str) -> list[tuple[str, str]]:
    return re.findall(r'\("([^"]+)",\s*"([^"]+)"\)', text)


def main() -> None:
    style_text = STYLE.read_text(encoding="utf-8")
    gen_text = MEMBERS_OUT.read_text(encoding="utf-8")

    existing_classes = parse_members(
        re.search(r"CLASS_RENAMES = \[(.*?)\]", style_text, re.S).group(1)
    )
    existing_members = parse_members(
        re.search(r"MEMBER_RENAMES = \[(.*?)\]", style_text, re.S).group(1)
    )
    gen_members = parse_members(gen_text)

    class_map = {old: new for old, new in existing_classes}
    for old, new in PLATFORM_CLASSES:
        class_map[old] = new
    classes = sorted(class_map.items(), key=lambda x: (-len(x[0]), x[0]))

    member_map = {old: new for old, new in existing_members}
    for old, new in gen_members:
        member_map[old] = new
    members = sorted(member_map.items(), key=lambda x: (-len(x[0]), x[0]))

    def fmt_pairs(pairs: list[tuple[str, str]], indent: str) -> str:
        lines = [f"{indent}(\"{o}\", \"{n}\")," for o, n in pairs]
        return "\n".join(lines)

    style_text = re.sub(
        r"CLASS_RENAMES = \[.*?\]",
        "CLASS_RENAMES = [\n" + fmt_pairs(classes, "    ") + "\n]",
        style_text,
        count=1,
        flags=re.S,
    )
    style_text = re.sub(
        r"MEMBER_RENAMES = \[.*?\]",
        "MEMBER_RENAMES = [\n" + fmt_pairs(members, "    ") + "\n]",
        style_text,
        count=1,
        flags=re.S,
    )
    STYLE.write_text(style_text, encoding="utf-8", newline="\n")
    print(f"Updated {STYLE.name}: {len(classes)} classes, {len(members)} members")


if __name__ == "__main__":
    main()
