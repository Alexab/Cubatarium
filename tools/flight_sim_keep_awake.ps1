param(
  [string]$ProcessName = 'Cubatarium',
  [int]$PollSeconds = 10,
  [int]$StartTimeoutSeconds = 180
)

$ErrorActionPreference = 'Stop'

$nativeSource = @"
using System.Runtime.InteropServices;
public static class CubatariumFlightPowerRequest
{
    [DllImport("kernel32.dll")]
    public static extern uint SetThreadExecutionState(uint executionState);
}
"@

if (-not ('CubatariumFlightPowerRequest' -as [type])) {
  Add-Type -TypeDefinition $nativeSource
}

$esContinuous = [uint32]2147483648
$esSystemRequired = [uint32]1
$esDisplayRequired = [uint32]2
$awakeFlags = $esContinuous -bor $esSystemRequired -bor $esDisplayRequired
$seenProcess = $false
$startDeadline = [DateTime]::UtcNow.AddSeconds([Math]::Max(1, $StartTimeoutSeconds))
$pollDelay = [Math]::Max(1, $PollSeconds)

try {
  while ($true) {
    $process = Get-Process -Name $ProcessName -ErrorAction SilentlyContinue |
      Select-Object -First 1
    if ($process) {
      $seenProcess = $true
      [void][CubatariumFlightPowerRequest]::SetThreadExecutionState($awakeFlags)
    } elseif ($seenProcess) {
      break
    } elseif ([DateTime]::UtcNow -ge $startDeadline) {
      break
    }
    Start-Sleep -Seconds $pollDelay
  }
} finally {
  # Release the temporary request; no system power-plan setting is changed.
  [void][CubatariumFlightPowerRequest]::SetThreadExecutionState($esContinuous)
}
