#!/usr/bin/env python3
"""Audit coding style violations in src/. Exit 1 if any found."""

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent / "src"

# Legacy creature/visual classes pending U-prefix rename (TD-CRE-029).
LEGACY_CLASS_FILE_PREFIXES = (
    "src/Creatures/Visual/",
    "src/Pose/BoneSkeleton/",
    "src/Gui/Layout/DockedOverlayLayout.h",
)

SKIP_INCLUDES = {
    "Version.h",
    "ThirdParty/stb_image.h",
    "stb_image.h",
    "egl_context.h",
    "android_jni.h",
    "android_soft_keyboard.h",
}

CLASS_RE = re.compile(r"^class\s+([A-Z][a-z][A-Za-z0-9]*)", re.M)
MEMBER_RE = re.compile(r"\b[a-z][a-zA-Z0-9]*_\b")
BARE_INCLUDE_RE = re.compile(r'#include\s+"([^"/\\]+\.h)"')
STRUCT_FIELD_RE = re.compile(r"^\s+([a-z][a-zA-Z0-9]*)\s*[\[{;]", re.M)
STRUCT_BLOCK_RE = re.compile(r"\bstruct\s+\w+[^{]*\{", re.M)


def struct_field_violations(text: str) -> list[str]:
    issues = []
    skip_names = {
        "namespace", "class", "struct", "enum", "return", "using", "template",
        "public", "private", "protected", "virtual", "override", "const",
        "static", "inline", "friend", "typedef", "if", "for", "while",
        "continue", "break", "return",
    }
    for sm in STRUCT_BLOCK_RE.finditer(text):
        start = sm.end()
        depth = 1
        i = start
        while i < len(text) and depth > 0:
            if text[i] == "{":
                depth += 1
            elif text[i] == "}":
                depth -= 1
            i += 1
        block = text[start : i - 1]
        for m in STRUCT_FIELD_RE.finditer(block):
            field = m.group(1)
            if field in skip_names:
                continue
            pascal = field[0].upper() + field[1:]
            if field != pascal:
                issues.append(field)
    return issues


def audit_file(fp: Path) -> list[str]:
    if fp.name == "stb_image.h" or "ThirdParty" in str(fp):
        return []
    text = fp.read_text(encoding="utf-8", errors="ignore")
    rel = fp.relative_to(ROOT.parent)
    rel_posix = rel.as_posix()
    issues: list[str] = []

    skip_class_prefix = any(
        rel_posix.startswith(prefix) or rel_posix == prefix
        for prefix in LEGACY_CLASS_FILE_PREFIXES
    )

    if not skip_class_prefix:
        for m in CLASS_RE.finditer(text):
            name = m.group(1)
            if name.startswith("I") or name.startswith("U"):
                continue
            issues.append(f"{rel}: class without U/I prefix: {name}")

    if fp.suffix == ".h":
        for m in MEMBER_RE.finditer(text):
            issues.append(f"{rel}: trailing_ member: {m.group(0)}")

        for field in struct_field_violations(text):
            issues.append(f"{rel}: struct field not PascalCase: {field}")

    for m in BARE_INCLUDE_RE.finditer(text):
        inc = m.group(1).replace("\\", "/")
        if inc not in SKIP_INCLUDES:
            issues.append(f"{rel}: bare include: {inc}")

    return issues


def main() -> int:
    all_issues: list[str] = []
    for fp in sorted(ROOT.rglob("*")):
        if fp.suffix not in (".h", ".cpp"):
            continue
        all_issues.extend(audit_file(fp))

    if not all_issues:
        print("No style violations found.")
        return 0

    print(f"Found {len(all_issues)} violation(s):")
    for issue in all_issues[:100]:
        print(f"  {issue}")
    if len(all_issues) > 100:
        print(f"  ... and {len(all_issues) - 100} more")
    return 1


if __name__ == "__main__":
    raise SystemExit(main())
