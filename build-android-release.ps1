# Build Cubatarium release AAB for Google Play (delegates to platforms/android).
$ErrorActionPreference = "Stop"
$repoRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
$envScript = Join-Path $repoRoot "platforms\windows\setup-android-env.ps1"
if (Test-Path $envScript) {
    . $envScript
}

$androidDir = Join-Path $repoRoot "platforms\android"
$keystoreProps = Join-Path $androidDir "keystore.properties"
if (-not (Test-Path $keystoreProps)) {
    Write-Host "Warning: keystore.properties not found. Run platforms\android\setup-release-keystore.ps1"
    Write-Host "Release AAB will be unsigned and cannot be uploaded to Play."
}

Push-Location $androidDir
try {
    if (-not (Test-Path ".\gradlew.bat")) {
        Write-Error "Gradle wrapper not found. Run: gradle wrapper --gradle-version 8.11.1 (in platforms/android)"
    }
    & .\gradlew.bat bundleRelease @args
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
    $aab = Get-ChildItem -Recurse -Filter "cubatarium-*.aab" "app\build\outputs\bundle\release" -ErrorAction SilentlyContinue | Select-Object -First 1
    if (-not $aab) {
        $aab = Get-ChildItem -Recurse -Filter "app-release.aab" "app\build\outputs\bundle\release" -ErrorAction SilentlyContinue | Select-Object -First 1
    }
    if ($aab) {
        Write-Host "AAB: $($aab.FullName)"
    }
}
finally {
    Pop-Location
}
