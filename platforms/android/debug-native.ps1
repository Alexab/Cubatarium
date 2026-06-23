# Attach NDK lldb to Cubatarium on a connected device/emulator.
# Requires: debug APK installed, adb in PATH, ANDROID_HOME set.
$ErrorActionPreference = "Stop"

$repoRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$envScript = Join-Path $repoRoot "platforms\windows\setup-android-env.ps1"
if (Test-Path $envScript) { . $envScript }

$NDK = "$env:ANDROID_HOME\ndk\30.0.14904198"
$NDK_BIN = "$NDK\toolchains\llvm\prebuilt\windows-x86_64\bin"
$LLDB_SERVER = "$NDK\toolchains\llvm\prebuilt\windows-x86_64\lib\clang\21\lib\linux\x86_64\lldb-server"
$ABI = (adb shell getprop ro.product.cpu.abi).Trim()
if ($ABI -ne "x86_64") {
    Write-Warning "Native attach script tested on x86_64 emulator; device ABI is $ABI"
}

Remove-Item Env:PYTHONPATH -ErrorAction SilentlyContinue
Remove-Item Env:PYTHONHOME -ErrorAction SilentlyContinue
$env:PYTHONNOUSERSITE = "1"

adb root 2>$null | Out-Null
adb shell "pkill lldb-server" 2>$null
adb push $LLDB_SERVER /data/local/tmp/lldb-server | Out-Null
adb shell chmod 755 /data/local/tmp/lldb-server
adb forward tcp:5039 tcp:5039

Write-Host "Starting lldb-server..."
Start-Process -FilePath adb -ArgumentList "shell","/data/local/tmp/lldb-server","platform","--server","--listen","localhost:5039" -WindowStyle Hidden
Start-Sleep -Seconds 2

adb shell am force-stop com.cubatarium
adb shell am start -n com.cubatarium/.MainActivity | Out-Null
Start-Sleep -Seconds 3
$appPid = (adb shell pidof com.cubatarium).Trim()
if (-not $appPid) { throw "com.cubatarium is not running" }
Write-Host "Attached target PID: $appPid"

Write-Host @"

In the lldb prompt that opens, run:
  platform select remote-android
  platform connect connect://localhost:5039
  process attach --pid $appPid
  bt

If lldb crashes on attach, use Android Studio: Run > Debug APK (native symbols in debug build).
"@

Push-Location $NDK_BIN
try {
    & cmd /c "lldb.cmd"
}
finally {
    Pop-Location
}
