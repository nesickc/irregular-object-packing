[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$Executable,
    [Parameter(Mandatory = $true)][string]$Object,
    [Parameter(Mandatory = $true)][string]$Container,
    [Parameter(Mandatory = $true)][string]$OutputDirectory,
    [ValidateRange(1, 1000)][int[]]$Counts = @(10),
    [ValidateRange(1, 10)][int]$Repeats = 3,
    [uint32]$Seed = 1918,
    [ValidateRange(0.000001, 1.0)][double]$InitialScale = 0.1,
    [ValidateRange(0.000001, 1.0)][double]$FinalScale = 1.0,
    [ValidateRange(1, 200)][int]$ScaleSteps = 9,
    [ValidateRange(1, 300000)][int]$TimeoutMilliseconds = 300000,
    [ValidateRange(1, 300000)][int]$LocalTimeoutMilliseconds = 30000,
    [ValidateRange(1, 10000)][int]$LocalIterations = 1000,
    [ValidateRange(1, 10000000)][int]$Attempts = 1000000,
    [string]$Label = 'real-stl',
    [switch]$DisableAdaptiveSampling,
    [switch]$DisableInitializationFallback,
    [switch]$DetailedDiagnostics,
    [switch]$ContinueAfterFailure
)

$ErrorActionPreference = 'Stop'
$benchmarkExecutable = (Resolve-Path -LiteralPath $Executable).Path
$benchmarkObject = (Resolve-Path -LiteralPath $Object).Path
$benchmarkContainer = (Resolve-Path -LiteralPath $Container).Path
foreach ($benchmarkInput in @($benchmarkExecutable, $benchmarkObject, $benchmarkContainer)) {
    if (-not (Test-Path -LiteralPath $benchmarkInput -PathType Leaf)) {
        throw 'Executable, Object and Container must name existing files.'
    }
}
if ($InitialScale -gt $FinalScale) { throw 'InitialScale must not exceed FinalScale.' }
$benchmarkOutput = [System.IO.Path]::GetFullPath($OutputDirectory)
if (Test-Path -LiteralPath $benchmarkOutput) { throw 'OutputDirectory must be new.' }
$null = New-Item -ItemType Directory -Path $benchmarkOutput
$invariant = [System.Globalization.CultureInfo]::InvariantCulture

function Get-Distribution {
    param([object[]]$Values)
    $sorted = @($Values | Where-Object { $null -ne $_ } | ForEach-Object { [double]$_ } | Sort-Object)
    if ($sorted.Count -eq 0) { return $null }
    $middle = [int][Math]::Floor($sorted.Count / 2)
    $median = $sorted[$middle]
    if ($sorted.Count % 2 -eq 0) { $median = ($sorted[$middle - 1] + $sorted[$middle]) / 2 }
    return [ordered]@{ samples = $sorted.Count; minimum = $sorted[0]; median = $median; maximum = $sorted[-1] }
}

$cases = @()
foreach ($count in $Counts) {
    $reports = @()
    for ($repeat = 1; $repeat -le $Repeats; $repeat++) {
        $caseId = "pack-$count-$repeat"
        $reportPath = Join-Path $benchmarkOutput "$caseId.json"
        $logPath = Join-Path $benchmarkOutput "$caseId.log"
        $runOutput = Join-Path $benchmarkOutput "$caseId-run"
        $arguments = @(
            'pack', '--object', $benchmarkObject, '--container', $benchmarkContainer,
            '--count', [string]$count, '--seed', [string]$Seed,
            '--initial-scale', $InitialScale.ToString('R', $invariant),
            '--final-scale', $FinalScale.ToString('R', $invariant),
            '--scale-steps', [string]$ScaleSteps, '--timeout-ms', [string]$TimeoutMilliseconds,
            '--local-timeout-ms', [string]$LocalTimeoutMilliseconds,
            '--local-iterations', [string]$LocalIterations, '--attempts', [string]$Attempts,
            '--label', $Label, '--output', $reportPath, '--run-output', $runOutput
        )
        if ($DisableAdaptiveSampling) { $arguments += '--no-adaptive-sampling' }
        if ($DisableInitializationFallback) { $arguments += '--no-initialization-fallback' }
        if ($DetailedDiagnostics) { $arguments += '--detailed-diagnostics' }
        & $benchmarkExecutable @arguments *> $logPath
        if ($LASTEXITCODE -ne 0 -or -not (Test-Path -LiteralPath $reportPath -PathType Leaf)) {
            throw "Benchmark $caseId failed to publish a report; inspect $logPath."
        }
        $reports += Get-Content -LiteralPath $reportPath -Raw | ConvertFrom-Json
    }
    $stages = [ordered]@{}
    $stageNames = @($reports | ForEach-Object {
        if ($null -ne $_.stages) { $_.stages.PSObject.Properties.Name }
    } | Sort-Object -Unique)
    foreach ($stage in $stageNames) {
        $stages[$stage] = [ordered]@{
            wall_ms = Get-Distribution @($reports | ForEach-Object { $_.stages.$stage.wall_ms })
            cpu_ms = Get-Distribution @($reports | ForEach-Object { $_.stages.$stage.cpu_ms })
        }
    }
    $successes = @($reports | Where-Object {
        $_.status -eq 'success' -and $_.completed_at_exact_target -and $_.physically_valid -and
        $_.saved_run.status -eq 'loaded' -and $_.saved_run.has_geometry
    }).Count
    $cases += [ordered]@{
        object_count = $count
        repeats = $Repeats
        validated_and_loaded_successes = $successes
        success_fraction = $successes / [double]$Repeats
        statuses = @($reports | Group-Object status | ForEach-Object {
            [ordered]@{ status = $_.Name; count = $_.Count }
        })
        total_wall_ms = Get-Distribution @($reports | ForEach-Object { $_.total.wall_ms })
        total_cpu_ms = Get-Distribution @($reports | ForEach-Object { $_.total.cpu_ms })
        peak_working_set_bytes = Get-Distribution @($reports | ForEach-Object { $_.memory.peak_working_set_bytes })
        peak_commit_bytes = Get-Distribution @($reports | ForEach-Object { $_.memory.peak_commit_bytes })
        stages = $stages
    }
    Write-Output "$count objects: $successes/$Repeats validated, exported and loaded."
    if ($successes -ne $Repeats -and -not $ContinueAfterFailure) {
        Write-Output 'Stopping the count ladder after this case; all failure reports are retained.'
        break
    }
}
$summaryPath = Join-Path $benchmarkOutput 'summary.json'
[ordered]@{
    schema_version = 1
    label = $Label
    workload_kind = $(if ($InitialScale -eq $FinalScale) { 'direct_placement' } else { 'genuine_growth' })
    density_policy = 'same supplied container at every count; use separately scaled container files for fixed-density studies'
    aggregation = 'independent processes; input hashes warm file caches; timing distributions include unsuccessful outcomes; stage CPU null when unavailable'
    requested_counts = $Counts
    object = $benchmarkObject
    container = $benchmarkContainer
    executable = $benchmarkExecutable
    executable_sha256 = (Get-FileHash -LiteralPath $benchmarkExecutable -Algorithm SHA256).Hash.ToLowerInvariant()
    cases = $cases
} | ConvertTo-Json -Depth 15 | Set-Content -LiteralPath $summaryPath -Encoding utf8
Write-Output $summaryPath
