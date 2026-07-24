# Build Windows installer (staging + Actual Installer).
$ErrorActionPreference = "Stop"
$repoRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$installerScript = Join-Path $repoRoot "packaging\windows\installer\prepare-installer.ps1"
& $installerScript @args
exit $LASTEXITCODE
