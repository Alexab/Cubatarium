#!/usr/bin/env python3
"""Rewrite bare #include \"Foo.h\" to Module/.../Foo.h from src root."""

import re
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent / "src"

BARE_TO_PATH = {
    # Gui/Widgets
    "GuiWidget.h": "Gui/Widgets/GuiWidget.h",
    "GuiButton.h": "Gui/Widgets/GuiButton.h",
    "GuiCheckbox.h": "Gui/Widgets/GuiCheckbox.h",
    "GuiDialogFrame.h": "Gui/Widgets/GuiDialogFrame.h",
    "GuiLabel.h": "Gui/Widgets/GuiLabel.h",
    "GuiListView.h": "Gui/Widgets/GuiListView.h",
    "GuiPanel.h": "Gui/Widgets/GuiPanel.h",
    "GuiPopupMenu.h": "Gui/Widgets/GuiPopupMenu.h",
    "GuiScrollView.h": "Gui/Widgets/GuiScrollView.h",
    "GuiSlot.h": "Gui/Widgets/GuiSlot.h",
    "GuiTabBar.h": "Gui/Widgets/GuiTabBar.h",
    "GuiTextInput.h": "Gui/Widgets/GuiTextInput.h",
    "GuiWindow.h": "Gui/Widgets/GuiWindow.h",
    "WorldGenSettingsForm.h": "Gui/Widgets/WorldGenSettingsForm.h",
    # Gui/Screens
    "ConsoleScreen.h": "Gui/Screens/ConsoleScreen.h",
    "CreativePaletteScreen.h": "Gui/Screens/CreativePaletteScreen.h",
    "InGameHudScreen.h": "Gui/Screens/InGameHudScreen.h",
    "LoadWorldScreen.h": "Gui/Screens/LoadWorldScreen.h",
    "MainMenuScreen.h": "Gui/Screens/MainMenuScreen.h",
    "NewWorldScreen.h": "Gui/Screens/NewWorldScreen.h",
    "SettingsScreen.h": "Gui/Screens/SettingsScreen.h",
    # Gui
    "GuiLayout.h": "Gui/Layout/GuiLayout.h",
    # Other modules
    "CommandRegistry.h": "Commands/CommandRegistry.h",
    "ContentType.h": "Content/ContentType.h",
    "ContentTypeRegistry.h": "Content/ContentTypeRegistry.h",
    "GameSession.h": "Game/GameSession.h",
}

SKIP_INCLUDES = {
    "Version.h",
    "ThirdParty/stb_image.h",
    "stb_image.h",
    "egl_context.h",
    "android_jni.h",
    "android_soft_keyboard.h",
}

INCLUDE_RE = re.compile(r'#include\s+"([^"]+)"')


def main() -> int:
    changed = 0
    for fp in sorted(ROOT.rglob("*")):
        if fp.suffix not in (".h", ".cpp"):
            continue
        text = fp.read_text(encoding="utf-8")
        orig = text

        def repl(m: re.Match) -> str:
            path = m.group(1).replace("\\", "/")
            if path in SKIP_INCLUDES or "/" in path:
                return m.group(0)
            if path in BARE_TO_PATH:
                return f'#include "{BARE_TO_PATH[path]}"'
            return m.group(0)

        text = INCLUDE_RE.sub(repl, text)
        if text != orig:
            fp.write_text(text, encoding="utf-8", newline="\n")
            changed += 1
    print(f"Fixed bare includes in {changed} files")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
