[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$Executable,
    [Parameter(Mandatory = $true)]
    [string]$OutputDirectory,
    [ValidateRange(1, 10)]
    [int]$Repeats = 3,
    [string]$Label = 'measurement',
    [switch]$DisableInitializationFallback
)

$ErrorActionPreference = 'Stop'
$benchmarkExecutable = (Resolve-Path -LiteralPath $Executable).Path
if (-not (Test-Path -LiteralPath $benchmarkExecutable -PathType Leaf)) {
    throw 'Executable must name the built irop_benchmarks executable.'
}
$benchmarkOutput = [System.IO.Path]::GetFullPath($OutputDirectory)
if (Test-Path -LiteralPath $benchmarkOutput) {
    throw 'OutputDirectory must be new so earlier benchmark evidence is retained.'
}
$null = New-Item -ItemType Directory -Path $benchmarkOutput

# Keep cases small enough for deliberate local profiling. Dense initialization
# remains bounded even when its requested object count cannot be placed.
$cases = @(
    @{ Id = 'sparse-10-s12'; Name = 'init-sparse'; Count = 10; Segments = 12 },
    @{ Id = 'sparse-36-s12'; Name = 'init-sparse'; Count = 36; Segments = 12 },
    @{ Id = 'dense-10-s12'; Name = 'init-dense'; Count = 10; Segments = 12 },
    @{ Id = 'dense-36-s12'; Name = 'init-dense'; Count = 36; Segments = 12 },
    @{ Id = 'collision-10-s12'; Name = 'collision'; Count = 10; Segments = 12 },
    @{ Id = 'collision-36-s12'; Name = 'collision'; Count = 36; Segments = 12 },
    @{ Id = 'collision-100-s12'; Name = 'collision'; Count = 100; Segments = 12 },
    @{ Id = 'collision-10-s48'; Name = 'collision'; Count = 10; Segments = 48 },
    @{ Id = 'collision-10-s96'; Name = 'collision'; Count = 10; Segments = 96 },
    @{ Id = 'stages'; Name = 'stages'; Count = 1; Segments = 12 },
    @{ Id = 'growth'; Name = 'growth'; Count = 1; Segments = 12 }
)

function Get-Median {
    param([double[]]$Values)
    $sorted = @($Values | Sort-Object)
    if ($sorted.Count -eq 0) {
        return $null
    }
    $middle = [int][Math]::Floor($sorted.Count / 2)
    if ($sorted.Count % 2 -eq 1) {
        return $sorted[$middle]
    }
    return ($sorted[$middle - 1] + $sorted[$middle]) / 2
}

$summaries = @()
foreach ($case in $cases) {
    $reports = @()
    for ($repeat = 1; $repeat -le $Repeats; $repeat++) {
        $reportPath = Join-Path $benchmarkOutput ($case.Id + '-' + $repeat + '.json')
        $logPath = Join-Path $benchmarkOutput ($case.Id + '-' + $repeat + '.log')
        $arguments = @(
            $case.Name, '--count', [string]$case.Count,
            '--segments', [string]$case.Segments,
            '--attempts', '1000000', '--seed', '1918', '--timeout-ms', '10000',
            '--label', $Label, '--output', $reportPath
        )
        if ($DisableInitializationFallback) {
            $arguments += '--no-initialization-fallback'
        }
        & $benchmarkExecutable @arguments *> $logPath
        if ($LASTEXITCODE -ne 0 -or -not (Test-Path -LiteralPath $reportPath -PathType Leaf)) {
            throw "Benchmark $($case.Id) repeat $repeat failed to publish its report; inspect $logPath."
        }
        $reports += Get-Content -LiteralPath $reportPath -Raw | ConvertFrom-Json
    }
    $stages = [ordered]@{}
    $stageNames = @($reports | ForEach-Object {
        if ($null -ne $_.stages) { $_.stages.PSObject.Properties.Name }
    } | Sort-Object -Unique)
    foreach ($stageName in $stageNames) {
        $completed = @($reports | ForEach-Object {
            if ($null -ne $_.stages.$stageName) { $_.stages.$stageName }
        })
        $stages[$stageName] = [ordered]@{
            completed_samples = $completed.Count
            median_wall_ms = Get-Median @($completed | ForEach-Object { $_.wall_ms })
            median_cpu_ms = Get-Median @($completed | ForEach-Object { $_.cpu_ms })
        }
    }
    $summary = [ordered]@{
        id = $case.Id
        repeats = $Repeats
        statuses = @($reports | Group-Object status | ForEach-Object {
            [ordered]@{ status = $_.Name; count = $_.Count }
        })
        median_wall_ms = Get-Median @($reports | ForEach-Object { $_.total.wall_ms })
        median_cpu_ms = Get-Median @($reports | ForEach-Object { $_.total.cpu_ms })
        median_peak_working_set_bytes = Get-Median @($reports | ForEach-Object { $_.memory.peak_working_set_bytes })
        median_peak_commit_bytes = Get-Median @($reports | ForEach-Object { $_.memory.peak_commit_bytes })
        stages = $stages
    }
    $summaries += $summary
    Write-Output "$($case.Id): $($summary.median_wall_ms) ms median wall"
}
$summaryPath = Join-Path $benchmarkOutput 'summary.json'
[ordered]@{
    schema_version = 1
    label = $Label
    executable = $benchmarkExecutable
    repeats = $Repeats
    aggregation = 'independent cold processes; median timing includes unsuccessful outcomes; inspect status counts'
    cases = $summaries
} | ConvertTo-Json -Depth 12 | Set-Content -LiteralPath $summaryPath -Encoding utf8
Write-Output $summaryPath