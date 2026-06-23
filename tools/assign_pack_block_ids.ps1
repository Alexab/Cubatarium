# Assign stable global block ids (1-4095) in resource_packs/*/blocks/*.json.
param(
    [switch]$Write,
    [switch]$Check,
    [switch]$DryRun
)

$ErrorActionPreference = "Stop"

$PackIdMin = 1
$PackIdMax = 4095

$repoRoot = Split-Path -Parent $PSScriptRoot
$packsRoot = Join-Path $repoRoot "resource_packs"

if (-not (Test-Path $packsRoot)) {
    Write-Error "resource_packs not found: $packsRoot"
}

if (-not $Write -and -not $Check -and -not $DryRun) {
    Write-Host "Usage: assign_pack_block_ids.ps1 -Write | -Check | -DryRun"
    exit 1
}

function Get-PackIdFromManifest {
    param([string]$PackDir)
    $packJson = Join-Path $PackDir "pack.json"
    if (-not (Test-Path $packJson)) {
        return $null
    }
    try {
        $manifest = Get-Content $packJson -Raw | ConvertFrom-Json
        if ($manifest.id) { return [string]$manifest.id }
    } catch {}
    return Split-Path -Leaf $PackDir
}

$entries = New-Object System.Collections.Generic.List[object]
$idOwners = @{}

Get-ChildItem $packsRoot -Directory | ForEach-Object {
    $packDir = $_.FullName
    $dirName = $_.Name
    if ($dirName.StartsWith("_")) {
        return
    }
    $blocksDir = Join-Path $packDir "blocks"
    if (-not (Test-Path $blocksDir)) {
        return
    }
    $packId = Get-PackIdFromManifest $packDir
    if (-not $packId) {
        Write-Warning "Skipping pack without id: $dirName"
        return
    }
    Get-ChildItem $blocksDir -Filter "*.json" | ForEach-Object {
        $path = $_.FullName
        try {
            $json = Get-Content $path -Raw | ConvertFrom-Json
        } catch {
            Write-Warning "Invalid JSON: $path"
            return
        }
        $name = [string]$json.name
        if ([string]::IsNullOrWhiteSpace($name)) {
            Write-Warning "Missing name: $path"
            return
        }
        $blockId = $null
        if ($null -ne $json.PSObject.Properties["id"]) {
            $rawId = $json.id
            if ($rawId -is [int] -or $rawId -is [long]) {
                $blockId = [int]$rawId
            }
        }
        $entries.Add([PSCustomObject]@{
            PackId   = $packId
            Name     = $name
            Path     = $path
            BlockId  = $blockId
        }) | Out-Null
    }
}

$usedIds = @{}
$duplicateIds = @()
$missing = @()

foreach ($entry in $entries) {
    if ($null -eq $entry.BlockId) {
        $missing += $entry
        continue
    }
    if ($entry.BlockId -lt $PackIdMin -or $entry.BlockId -gt $PackIdMax) {
        Write-Error "Block id out of range $($entry.BlockId): $($entry.Path)"
    }
    if ($usedIds.ContainsKey($entry.BlockId)) {
        $duplicateIds += [PSCustomObject]@{
            Id      = $entry.BlockId
            First   = $usedIds[$entry.BlockId]
            Second  = "$($entry.PackId)/$($entry.Name) ($($entry.Path))"
        }
    } else {
        $usedIds[$entry.BlockId] = "$($entry.PackId)/$($entry.Name) ($($entry.Path))"
    }
}

$nextId = $PackIdMin
if ($usedIds.Count -gt 0) {
    $nextId = ([int[]]$usedIds.Keys | Measure-Object -Maximum).Maximum + 1
}

$toAssign = $missing | Sort-Object PackId, Name
$assignments = @()

foreach ($entry in $toAssign) {
    while ($usedIds.ContainsKey($nextId)) {
        $nextId++
    }
    if ($nextId -gt $PackIdMax) {
        Write-Error "No free block ids left in range $PackIdMin-$PackIdMax"
    }
    $assignments += [PSCustomObject]@{
        PackId  = $entry.PackId
        Name    = $entry.Name
        Path    = $entry.Path
        BlockId = $nextId
    }
    $usedIds[$nextId] = "$($entry.PackId)/$($entry.Name) ($($entry.Path))"
    $nextId++
}

$hasErrors = $false

if ($duplicateIds.Count -gt 0) {
    $hasErrors = $true
    Write-Host "Duplicate block ids:"
    foreach ($dup in $duplicateIds) {
        Write-Host "  id $($dup.Id): $($dup.First) vs $($dup.Second)"
    }
}

if ($missing.Count -gt 0 -and $Check) {
    $hasErrors = $true
    Write-Host "Blocks missing id: $($missing.Count)"
    foreach ($entry in $missing | Select-Object -First 20) {
        Write-Host "  $($entry.PackId)/$($entry.Name)"
    }
    if ($missing.Count -gt 20) {
        Write-Host "  ..."
    }
}

if ($DryRun) {
    Write-Host "Would assign $($assignments.Count) block id(s):"
    foreach ($a in $assignments) {
        Write-Host "  $($a.BlockId) -> $($a.PackId)/$($a.Name)"
    }
    if ($hasErrors) { exit 1 }
    exit 0
}

if ($Check) {
    if ($hasErrors) { exit 1 }
    Write-Host "Block id check OK ($($entries.Count) blocks, $($usedIds.Count) ids)"
    exit 0
}

if (-not $Write) {
    exit 0
}

foreach ($a in $assignments) {
    $obj = Get-Content $a.Path -Raw | ConvertFrom-Json
    $ordered = [ordered]@{}
    $inserted = $false
    foreach ($prop in $obj.PSObject.Properties) {
        if (-not $inserted -and $prop.Name -eq "name") {
            $ordered["name"] = $prop.Value
            $ordered["id"] = $a.BlockId
            $inserted = $true
        } elseif ($prop.Name -ne "id") {
            $ordered[$prop.Name] = $prop.Value
        }
    }
    if (-not $inserted) {
        $ordered["id"] = $a.BlockId
    }
    ($ordered | ConvertTo-Json -Depth 20) + "`n" | Set-Content -Path $a.Path -Encoding UTF8
    Write-Host "Wrote id $($a.BlockId) -> $($a.PackId)/$($a.Name)"
}

if ($hasErrors) {
    exit 1
}

Write-Host "Assigned $($assignments.Count) block id(s)."
