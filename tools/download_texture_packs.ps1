param(
    [string]$OutRoot = "E:\Work\Home\CubatariumTextureResearch"
)

$ErrorActionPreference = "Stop"
$ScriptDir = $PSScriptRoot
$PythonScript = Join-Path $ScriptDir "download_texture_packs.py"

if (-not (Test-Path $PythonScript)) {
    Write-Error "Missing helper: $PythonScript"
    exit 1
}

python $PythonScript --out-root $OutRoot
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
Write-Host "Texture pack download complete: $OutRoot"
