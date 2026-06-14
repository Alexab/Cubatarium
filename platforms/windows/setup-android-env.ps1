#Requires -Version 5.1
<#
.SYNOPSIS
  Configures user environment variables for Cubatarium Android development.

.DESCRIPTION
  Sets ANDROID_HOME, ANDROID_SDK_ROOT, JAVA_HOME (Android Studio JBR) and
  prepends SDK/NDK tool paths to the user PATH. Also updates the current
  session so changes apply without restarting the terminal.

.EXAMPLE
  powershell -ExecutionPolicy Bypass -File platforms/windows/setup-android-env.ps1
#>
param(
    [switch]$WhatIf
)

$ErrorActionPreference = 'Stop'

function Write-Step([string]$Message) {
    Write-Host ">> $Message" -ForegroundColor Cyan
}

function Find-AndroidSdk {
    $candidates = @(
        $env:ANDROID_HOME,
        $env:ANDROID_SDK_ROOT,
        (Join-Path $env:LOCALAPPDATA 'Android\Sdk'),
        (Join-Path $env:USERPROFILE 'AppData\Local\Android\Sdk')
    ) | Where-Object { $_ -and (Test-Path $_) } | Select-Object -Unique

    foreach ($path in $candidates) {
        if (Test-Path (Join-Path $path 'platform-tools\adb.exe')) {
            return (Resolve-Path $path).Path
        }
    }
    return $null
}

function Find-AndroidStudioJbr {
    $candidates = @(
        $env:JAVA_HOME,
        'C:\Program Files\Android\Android Studio\jbr'
    )

    foreach ($item in $candidates) {
        if (-not $item) { continue }
        $java = Join-Path $item 'bin\java.exe'
        if (Test-Path $java) {
            return (Resolve-Path $item).Path
        }
    }
    return $null
}

function Find-CmakeBin {
    $candidates = @(
        'C:\Program Files\CMake\bin',
        'C:\Program Files (x86)\CMake\bin'
    )
    foreach ($path in $candidates) {
        if (Test-Path (Join-Path $path 'cmake.exe')) {
            return $path
        }
    }
    return $null
}

function Add-PathEntries {
    param(
        [string]$Scope,
        [string[]]$Entries
    )

    $current = [Environment]::GetEnvironmentVariable('Path', $Scope)
    if (-not $current) { $current = '' }

    $parts = $current -split ';' | Where-Object { $_ -and $_.Trim() -ne '' }
    $tail = New-Object System.Collections.Generic.List[string]
    $prefix = New-Object System.Collections.Generic.List[string]

    foreach ($entry in $Entries) {
        if (-not $entry) { continue }
        $resolved = $entry
        if (Test-Path $entry) {
            $resolved = (Resolve-Path $entry).Path
        }
        $prefix.Add($resolved)
    }

    foreach ($p in $parts) {
        $resolved = $p
        if (Test-Path $p) {
            $resolved = (Resolve-Path $p).Path
        }
        $duplicate = $false
        foreach ($pref in $prefix) {
            if ($pref -ieq $resolved) { $duplicate = $true; break }
        }
        if (-not $duplicate) {
            $tail.Add($resolved)
        }
    }

    $newPath = (($prefix + $tail) -join ';')
    if ($WhatIf) {
        Write-Host "[WhatIf] Path ($Scope): $newPath"
        return
    }
    [Environment]::SetEnvironmentVariable('Path', $newPath, $Scope)
}

function Set-UserEnvVar {
    param(
        [string]$Name,
        [string]$Value
    )
    if ($WhatIf) {
        Write-Host "[WhatIf] $Name=$Value"
        return
    }
    [Environment]::SetEnvironmentVariable($Name, $Value, 'User')
    Set-Item -Path "Env:$Name" -Value $Value
}

Write-Step 'Detecting Android SDK and JDK'
$sdk = Find-AndroidSdk
$jbr = Find-AndroidStudioJbr
$cmake = Find-CmakeBin

if (-not $sdk) {
    throw "Android SDK not found. Install Android Studio and SDK Platform Tools first."
}
if (-not $jbr) {
    throw "Android Studio JBR not found at 'C:\Program Files\Android\Android Studio\jbr'."
}

Write-Host "  ANDROID_HOME -> $sdk"
Write-Host "  JAVA_HOME    -> $jbr"
if ($cmake) {
    Write-Host "  CMake        -> $cmake"
}

$pathEntries = New-Object System.Collections.Generic.List[string]
[void]$pathEntries.Add((Join-Path $jbr 'bin'))
[void]$pathEntries.Add((Join-Path $sdk 'platform-tools'))
[void]$pathEntries.Add((Join-Path $sdk 'emulator'))

$cmdlineLatest = Join-Path $sdk 'cmdline-tools\latest\bin'
$cmdlineTools = Get-ChildItem (Join-Path $sdk 'cmdline-tools') -Directory -ErrorAction SilentlyContinue |
    Where-Object { $_.Name -ne 'latest' } |
    Sort-Object Name -Descending |
    Select-Object -First 1
if (Test-Path $cmdlineLatest) {
    [void]$pathEntries.Add($cmdlineLatest)
}
elseif ($cmdlineTools) {
    [void]$pathEntries.Add((Join-Path $cmdlineTools.FullName 'bin'))
}

if ($cmake) {
    [void]$pathEntries.Add($cmake)
}

Write-Step 'Writing user environment variables'
Set-UserEnvVar -Name 'ANDROID_HOME' -Value $sdk
Set-UserEnvVar -Name 'ANDROID_SDK_ROOT' -Value $sdk
Set-UserEnvVar -Name 'JAVA_HOME' -Value $jbr

Write-Step 'Updating user PATH (prepend SDK/JBR tools)'
Add-PathEntries -Scope 'User' -Entries $pathEntries.ToArray()

Write-Step 'Refreshing current session PATH'
$sessionPrefix = ($pathEntries | Where-Object { $_ }) -join ';'
$env:ANDROID_HOME = $sdk
$env:ANDROID_SDK_ROOT = $sdk
$env:JAVA_HOME = $jbr
$env:Path = "$sessionPrefix;$env:Path"

function Invoke-VersionCheck {
    param(
        [string]$Label,
        [string]$Exe,
        [string[]]$ArgumentList = @('-version')
    )
    Write-Host ''
    Write-Host "${Label}:"
    $prev = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    & $Exe @ArgumentList 2>&1 | ForEach-Object { Write-Host "  $_" }
    $ErrorActionPreference = $prev
}

Write-Step 'Verification'
Invoke-VersionCheck -Label 'JAVA_HOME\bin\java -version' -Exe (Join-Path $jbr 'bin\java.exe') -ArgumentList @('-version')
Invoke-VersionCheck -Label 'java -version (from PATH)' -Exe 'java' -ArgumentList @('-version')
Invoke-VersionCheck -Label 'adb version' -Exe (Join-Path $sdk 'platform-tools\adb.exe') -ArgumentList @('version')

$sdkmanager = Get-ChildItem $sdk -Recurse -Filter 'sdkmanager.bat' -ErrorAction SilentlyContinue |
    Select-Object -First 1
if ($sdkmanager) {
    Write-Host ''
    Write-Host "sdkmanager: $($sdkmanager.FullName)"
    Invoke-VersionCheck -Label 'sdkmanager --version' -Exe $sdkmanager.FullName -ArgumentList @('--version')
}
else {
    Write-Warning 'sdkmanager not found. Install "Android SDK Command-line Tools" in Android Studio SDK Manager.'
}

$ndkRoot = Join-Path $sdk 'ndk'
if (Test-Path $ndkRoot) {
    Write-Host ''
    Write-Host 'NDK installed:'
    Get-ChildItem $ndkRoot -Directory | ForEach-Object { Write-Host "  $($_.Name)" }
}
else {
    Write-Warning 'NDK not installed. Install "NDK (Side by side)" in Android Studio SDK Manager.'
}

$cmakeSdk = Join-Path $sdk 'cmake'
if (Test-Path $cmakeSdk) {
    Write-Host ''
    Write-Host 'CMake (SDK) installed:'
    Get-ChildItem $cmakeSdk -Directory | ForEach-Object { Write-Host "  $($_.Name)" }
}
else {
    Write-Warning 'CMake (SDK) not installed. Install "CMake" in Android Studio SDK Manager.'
}

Write-Host ''
Write-Host 'Done. User environment variables updated.' -ForegroundColor Green
Write-Host 'Open a NEW cmd/PowerShell window for a clean PATH.'
Write-Host ''
Write-Host 'If "java -version" still shows Java 8 in cmd, run as Administrator:'
Write-Host '  powershell -ExecutionPolicy Bypass -File platforms\windows\fix-java-precedence.ps1'
Write-Host 'Gradle/Android builds use JAVA_HOME regardless.'
