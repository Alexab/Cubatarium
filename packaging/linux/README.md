# Linux desktop

Run Cubatarium from a local build (`bin/Cubatarium`). No distro package yet.

## Build

```bash
./scripts/build/linux-configure.sh Release
cmake --build build/desktop-linux
```

## Menu launcher (optional)

```bash
REPO_ROOT="$(pwd)"
mkdir -p ~/.local/share/applications
mkdir -p ~/.local/share/icons/hicolor/256x256/apps
cp packaging/linux/cubatarium.desktop ~/.local/share/applications/
sed -i "s|Exec=bin/Cubatarium|Exec=${REPO_ROOT}/bin/Cubatarium|" ~/.local/share/applications/cubatarium.desktop
cp packaging/linux/hicolor/256x256/apps/cubatarium.png ~/.local/share/icons/hicolor/256x256/apps/
update-desktop-database ~/.local/share/applications 2>/dev/null || true
```

Window title-bar icon: `bin/icon.png` (copied at build time from `resources/branding/icon-64.png`).

Regenerate icon: `scripts/branding/generate-app-icons.ps1`.
