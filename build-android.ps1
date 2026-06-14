# Build Cubatarium debug APK (delegates to platforms/android).
$ErrorActionPreference = "Stop"
$repoRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
$envScript = Join-Path $repoRoot "platforms\windows\setup-android-env.ps1"
if (Test-Path $envScript) {
    . $envScript
}

$androidDir = Join-Path $repoRoot "platforms\android"
Push-Location $androidDir
try {
    if (-not (Test-Path ".\gradlew.bat")) {
        Write-Error "Gradle wrapper not found. Run: gradle wrapper --gradle-version 8.11.1 (in platforms/android)"
    }
    & .\gradlew.bat assembleDebug @args
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
    $apk = Get-ChildItem -Recurse -Filter "app-debug.apk" "app\build\outputs\apk\debug" -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($apk) {
        Write-Host "APK: $($apk.FullName)"
    }
}
finally {
    Pop-Location
}
