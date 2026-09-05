[CmdletBinding()]
param(
    [ValidateNotNullOrEmpty()]
    [string] $Preset = "windows-vs2026",
    [switch] $BuildBenchmarks,
    [switch] $BuildUI
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

function Invoke-Configure {
    param(
        [Parameter(Mandatory)]
        [string] $ConfigurePreset
    )

    $lines = [System.Collections.Generic.List[string]]::new()
    $configureArguments = @('--preset', $ConfigurePreset)
    if ($BuildBenchmarks) { $configureArguments += '-DIROP_BUILD_BENCHMARKS=ON' }
    if ($BuildUI) { $configureArguments += '-DIROP_BUILD_UI=ON' }
    & cmake @configureArguments 2>&1 | ForEach-Object {
        $line = "$_"
        $lines.Add($line)
        Write-Host $line
    }
    return @{
        ExitCode = $LASTEXITCODE
        Output = $lines -join [Environment]::NewLine
    }
}

$result = Invoke-Configure -ConfigurePreset $Preset
if ($result.ExitCode -eq 0) {
    exit 0
}

if ($result.Output -notmatch "(?i)(gl2ps-1\.4\.2|geuz\.org)") {
    Write-Error "CMake configure failed for a reason unrelated to the documented GL2PS endpoint."
    exit $result.ExitCode
}

if (-not $env:VCPKG_ROOT) {
    throw "VCPKG_ROOT is required for the hash-verified GL2PS cache fallback."
}
$vcpkgExecutable = Join-Path $env:VCPKG_ROOT "vcpkg.exe"
if (-not (Test-Path -LiteralPath $vcpkgExecutable -PathType Leaf)) {
    throw "vcpkg.exe was not found at $vcpkgExecutable."
}

$downloadPath = Join-Path $env:VCPKG_ROOT "downloads/gl2ps-1.4.2.tgz"
Write-Warning "The official GL2PS endpoint failed; seeding its exact SHA-512 into the vcpkg download cache."
& $vcpkgExecutable x-download $downloadPath --sha512=46652e1b3825ace61dbd77c4b0bf451e7671c248eb18bbd3369e2fac00056ea4cd5d2578561984313c239e3b02f78b9d9a76d963c935af65a13bc2abfc538620 --url=https://distfiles.gentoo.org/distfiles/04/gl2ps-1.4.2.tgz
if ($LASTEXITCODE -ne 0) {
    throw "The hash-verified GL2PS download-cache fallback failed."
}

$result = Invoke-Configure -ConfigurePreset $Preset
exit $result.ExitCode
