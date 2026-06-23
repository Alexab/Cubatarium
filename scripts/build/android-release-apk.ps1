# Build Cubatarium release APK for store distribution (Xiaomi, RuStore, sideload).
param(
    [switch]$Verify
)

$ErrorActionPreference = "Stop"
$repoRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$envScript = Join-Path $repoRoot "scripts\env\setup-android-env.ps1"
if (Test-Path $envScript) {
    . $envScript
}

$androidDir = Join-Path $repoRoot "platforms\android"
$keystoreProps = Join-Path $androidDir "keystore.properties"
if (-not (Test-Path $keystoreProps)) {
    Write-Host "Warning: keystore.properties not found. Run scripts\android\setup-release-keystore.ps1"
    Write-Host "Release APK will be unsigned and cannot be uploaded to stores."
}

Push-Location $androidDir
try {
    if (-not (Test-Path ".\gradlew.bat")) {
        Write-Error "Gradle wrapper not found. Run: gradle wrapper --gradle-version 8.11.1 (in platforms/android)"
    }
    & .\gradlew.bat assembleRelease @args
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

    $apkDir = "app\build\outputs\apk\release"
    $apk = Get-ChildItem -Path $apkDir -Filter "cubatarium-*.apk" -ErrorAction SilentlyContinue |
        Sort-Object LastWriteTime -Descending |
        Select-Object -First 1
    if (-not $apk) {
        $apk = Get-ChildItem -Path $apkDir -Filter "app-release.apk" -ErrorAction SilentlyContinue |
            Select-Object -First 1
    }
    if (-not $apk) {
        Write-Error "Release APK not found under $apkDir"
    }

    Write-Host "APK: $($apk.FullName)"

    if ($Verify) {
        $apksigner = Get-Command apksigner -ErrorAction SilentlyContinue
        if (-not $apksigner) {
            $sdkRoot = $env:ANDROID_HOME
            if (-not $sdkRoot) { $sdkRoot = $env:ANDROID_SDK_ROOT }
            if ($sdkRoot) {
                $candidates = Get-ChildItem (Join-Path $sdkRoot "build-tools") -Directory -ErrorAction SilentlyContinue |
                    Sort-Object Name -Descending |
                    ForEach-Object { Join-Path $_.FullName "apksigner.bat" } |
                    Where-Object { Test-Path $_ }
                if ($candidates) { $apksigner = $candidates[0] }
            }
        }
        if (-not $apksigner) {
            Write-Warning "apksigner not found; skip -Verify (install Android SDK build-tools)"
        } else {
            Write-Host ">> apksigner verify: $($apk.FullName)"
            if ($apksigner -is [System.String]) {
                & $apksigner verify --verbose $apk.FullName
            } else {
                & apksigner verify --verbose $apk.FullName
            }
            if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
        }
    }
}
finally {
    Pop-Location
}
