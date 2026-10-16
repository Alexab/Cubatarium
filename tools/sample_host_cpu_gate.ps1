[CmdletBinding()]
param(
    [ValidateRange(2, 120)]
    [int]$SampleCount = 10,

    [ValidateRange(100, 10000)]
    [int]$IntervalMs = 1000,

    [switch]$Json
)

$ErrorActionPreference = 'Stop'

if (-not ('CubatariumHostCpuSampler' -as [type])) {
    Add-Type -TypeDefinition @'
using System;
using System.ComponentModel;
using System.Runtime.InteropServices;

[StructLayout(LayoutKind.Sequential)]
public struct CubatariumCpuFileTime
{
    public uint Low;
    public uint High;
    public ulong Value { get { return ((ulong)High << 32) | Low; } }
}

public static class CubatariumHostCpuSampler
{
    [DllImport("kernel32.dll", SetLastError = true)]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static extern bool GetSystemTimes(
        out CubatariumCpuFileTime idle,
        out CubatariumCpuFileTime kernel,
        out CubatariumCpuFileTime user);

    public static ulong[] Read()
    {
        CubatariumCpuFileTime idle;
        CubatariumCpuFileTime kernel;
        CubatariumCpuFileTime user;
        if (!GetSystemTimes(out idle, out kernel, out user))
        {
            throw new Win32Exception(Marshal.GetLastWin32Error());
        }
        return new ulong[] { idle.Value, kernel.Value, user.Value };
    }
}
'@
}

$samples = [System.Collections.Generic.List[double]]::new()
for ($i = 0; $i -lt $SampleCount; $i++) {
    $before = [CubatariumHostCpuSampler]::Read()
    Start-Sleep -Milliseconds $IntervalMs
    $after = [CubatariumHostCpuSampler]::Read()

    $idleDelta = [double]($after[0] - $before[0])
    $totalDelta = [double](($after[1] - $before[1]) + ($after[2] - $before[2]))
    if ($totalDelta -le 0) {
        throw 'Windows returned a zero-length CPU sampling interval.'
    }

    $samples.Add([math]::Round((1.0 - ($idleDelta / $totalDelta)) * 100.0, 1))
}

$ordered = @($samples | Sort-Object)
$middle = $ordered.Count / 2
if (($ordered.Count % 2) -eq 0) {
    $median = ($ordered[$middle - 1] + $ordered[$middle]) / 2.0
}
else {
    $median = $ordered[[math]::Floor($middle)]
}
$p95Index = [math]::Min($ordered.Count - 1, [math]::Ceiling($ordered.Count * 0.95) - 1)

$result = [pscustomobject]@{
    sample_count = $SampleCount
    interval_ms = $IntervalMs
    samples_percent = @($samples)
    median_percent = [math]::Round($median, 1)
    p95_percent = $ordered[$p95Index]
    max_percent = $ordered[-1]
    passes_default_flight_gate = ([math]::Round($median, 1) -le 50.0 -and $ordered[$p95Index] -le 75.0)
}

if ($Json) {
    $result | ConvertTo-Json -Depth 3
}
else {
    $result | Format-List
}
