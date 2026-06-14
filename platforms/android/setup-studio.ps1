# Prepare and open Cubatarium in Android Studio (Java + native debug).
param(
    [switch]$NoLaunch
)

$ErrorActionPreference = "Stop"
$androidDir = $PSScriptRoot
$repoRoot = Split-Path -Parent (Split-Path -Parent $androidDir)

$envScript = Join-Path $repoRoot "platforms\windows\setup-android-env.ps1"
if (Test-Path $envScript) {
    . $envScript
}

if (-not $env:ANDROID_HOME) {
    throw "ANDROID_HOME is not set. Run platforms\windows\setup-android-env.ps1 first."
}

$sdkEscaped = ($env:ANDROID_HOME -replace '\\', '/').Replace(':', '\:')
$localProps = Join-Path $androidDir "local.properties"
Set-Content -Path $localProps -Encoding ASCII -Value "sdk.dir=$sdkEscaped"
Write-Host "Wrote $localProps"

$studioCandidates = @(
    "${env:ProgramFiles}\Android\Android Studio\bin\studio64.exe",
    "${env:ProgramFiles(x86)}\Android\Android Studio\bin\studio64.exe",
    "${env:LOCALAPPDATA}\Programs\Android Studio\bin\studio64.exe"
)
$studio = $studioCandidates | Where-Object { Test-Path $_ } | Select-Object -First 1
if (-not $studio) {
    throw "Android Studio not found. Install it or open '$androidDir' manually."
}

Write-Host @"

Android Studio setup:
  1. Open folder: $androidDir
  2. Wait for Gradle Sync (CMake root: repo CMakeLists.txt)
  3. Select run config 'app' and device/emulator (x86_64 AVD on Windows)
  4. Use Debug (Shift+F9), not Run — Dual debugger (Java + Native)
  5. Native breakpoints: android_main, RunCubatarium, AppRunner.cpp, egl_context.cpp

"@

if (-not $NoLaunch) {
    Write-Host "Launching Android Studio..."
    Start-Process -FilePath $studio -ArgumentList "`"$androidDir`""
}
