#Requires -Version 5.1
#Requires -RunAsAdministrator
<#
.SYNOPSIS
  Removes Oracle Java 8 shims from SYSTEM Path so Android Studio JBR (User Path) wins.

.EXAMPLE
  powershell -ExecutionPolicy Bypass -File platforms/windows/fix-java-precedence.ps1
#>
param([switch]$WhatIf)

$ErrorActionPreference = 'Stop'

$oraclePatterns = @(
    '*\Oracle\Java\java8path*',
    '*\Oracle\Java\javapath*'
)

$machinePath = [Environment]::GetEnvironmentVariable('Path', 'Machine')
if (-not $machinePath) {
    Write-Host 'System Path is empty. Nothing to do.'
    exit 0
}

$parts = $machinePath -split ';' | Where-Object { $_.Trim() -ne '' }
$kept = New-Object System.Collections.Generic.List[string]
$removed = New-Object System.Collections.Generic.List[string]

foreach ($part in $parts) {
    $drop = $false
    foreach ($pattern in $oraclePatterns) {
        if ($part -like $pattern) {
            $drop = $true
            break
        }
    }
    if ($drop) {
        $removed.Add($part)
    }
    else {
        $kept.Add($part)
    }
}

if ($removed.Count -eq 0) {
    Write-Host 'Oracle Java entries not found in System Path.'
    exit 0
}

Write-Host 'Will remove from System Path:'
$removed | ForEach-Object { Write-Host "  $_" }

if ($WhatIf) {
    Write-Host '[WhatIf] Skipping write.'
    exit 0
}

$newPath = ($kept -join ';')
[Environment]::SetEnvironmentVariable('Path', $newPath, 'Machine')
Write-Host 'System Path updated. Open a NEW cmd window and run: java -version' -ForegroundColor Green
