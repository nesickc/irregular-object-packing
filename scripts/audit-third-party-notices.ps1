[CmdletBinding()]
param(
    [Parameter(Mandatory)]
    [ValidateNotNullOrEmpty()]
    [string] $InstalledRoot,

    [ValidateNotNullOrEmpty()]
    [string] $BundleOutput
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

function Require-File {
    param(
        [Parameter(Mandatory)]
        [string] $Path,

        [Parameter(Mandatory)]
        [string] $Description
    )

    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
        throw "$Description was not found: $Path"
    }

    $item = Get-Item -LiteralPath $Path
    if ($item.Length -eq 0) {
        throw "$Description is empty: $Path"
    }

    return $item.FullName
}

$repositoryRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot "..")).Path
$inventoryPath = Require-File -Path (Join-Path $repositoryRoot "thirdparty/vcpkg-resolved-x64-windows.json") -Description "Resolved dependency inventory"
$projectLicense = Require-File -Path (Join-Path $repositoryRoot "LICENSE") -Description "Project license"
$thirdPartyNotice = Require-File -Path (Join-Path $repositoryRoot "THIRD_PARTY_NOTICES.md") -Description "Third-party notice"
$dependencyRecord = Require-File -Path (Join-Path $repositoryRoot "thirdparty/DEPENDENCIES.md") -Description "Dependency provenance record"
$releaseChecklist = Require-File -Path (Join-Path $repositoryRoot "docs/RELEASE_CHECKLIST.md") -Description "Release checklist"
$vcpkgOverlayLicense = Require-File -Path (Join-Path $repositoryRoot "thirdparty/LICENSES/vcpkg-MIT.txt") -Description "vcpkg overlay-source notice"
$vtkOverlayLicense = Require-File -Path (Join-Path $repositoryRoot "thirdparty/LICENSES/vtk-BSD-3-Clause.txt") -Description "VTK overlay-source notice"
$tetgenAdr = Require-File -Path (Join-Path $repositoryRoot "docs/adr/0009-tetgen-1-6-agpl-overlay-and-adapter.md") -Description "TetGen licensing ADR"
$ipoptAdr = Require-File -Path (Join-Path $repositoryRoot "docs/adr/0010-ipopt-3-14-19-official-windows-binary-adapter.md") -Description "Ipopt licensing ADR"

$inventory = Get-Content -LiteralPath $inventoryPath -Raw | ConvertFrom-Json
if ($inventory.schema_version -ne 1) {
    throw "Unsupported dependency inventory schema version: $($inventory.schema_version)"
}
if ($inventory.triplet -ne "x64-windows") {
    throw "The dependency inventory triplet must be x64-windows."
}

$manifestPath = Join-Path $repositoryRoot "vcpkg.json"
$configurationPath = Join-Path $repositoryRoot "vcpkg-configuration.json"
$manifest = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
$configuration = Get-Content -LiteralPath $configurationPath -Raw | ConvertFrom-Json
if ($manifest."builtin-baseline" -ne $inventory.builtin_registry) {
    throw "vcpkg.json builtin baseline does not match the audited inventory."
}
if ($configuration.PSObject.Properties.Name -contains "default-registry") {
    throw "The audited configuration must not override the manifest builtin registry."
}

$registryInventory = @($inventory.package_registry_overrides)
$configuredRegistries = @($configuration.registries)
$gl2psInventory = @($registryInventory | Where-Object package -eq "gl2ps")
$gl2psRegistries = @($configuredRegistries | Where-Object { @($_.packages) -contains "gl2ps" })
if ($registryInventory.Count -ne 1 -or $configuredRegistries.Count -ne 1 -or
    $gl2psInventory.Count -ne 1 -or $gl2psRegistries.Count -ne 1) {
    throw "The audited configuration must contain only the single GL2PS registry override."
}
if (@($gl2psRegistries[0].packages).Count -ne 1 -or $gl2psRegistries[0].packages[0] -ne "gl2ps") {
    throw "The audited GL2PS registry scope must contain exactly the gl2ps package."
}
if ($gl2psRegistries[0].baseline -ne $gl2psInventory[0].baseline) {
    throw "The GL2PS registry baseline does not match the audited inventory."
}
if ($gl2psRegistries[0].kind -ne "git") {
    throw "The GL2PS registry must use the audited git registry kind."
}
if ($gl2psRegistries[0].repository -ne $gl2psInventory[0].registry) {
    throw "The GL2PS registry repository does not match the audited inventory."
}
$overlayPorts = @($configuration."overlay-ports")
if ($overlayPorts.Count -ne 1 -or $overlayPorts[0] -ne "thirdparty/vcpkg-ports") {
    throw "The audited configuration must use only the checked-in thirdparty/vcpkg-ports overlay."
}

$installedRootPath = (Resolve-Path -LiteralPath $InstalledRoot).Path
$statusPath = Require-File -Path (Join-Path $installedRootPath "vcpkg/status") -Description "Installed vcpkg status database"
$records = (Get-Content -LiteralPath $statusPath -Raw) -split "\r?\n\r?\n"
$resolved = @{}

foreach ($record in $records) {
    $fields = @{}
    foreach ($line in ($record -split "\r?\n")) {
        if ($line -match "^([^:]+):\s*(.*)$") {
            $fields[$Matches[1]] = $Matches[2]
        }
    }

    if (-not $fields.ContainsKey("Package") -or $fields.ContainsKey("Feature")) {
        continue
    }

    foreach ($requiredField in @("Version", "Architecture", "Status")) {
        if (-not $fields.ContainsKey($requiredField)) {
            throw "Installed package record for $($fields['Package']) is missing $requiredField."
        }
    }
    if ($fields["Architecture"] -ne $inventory.triplet) {
        throw "Installed package $($fields['Package']) has architecture $($fields['Architecture']); expected $($inventory.triplet)."
    }
    if ($fields["Status"] -ne "install ok installed") {
        throw "Installed package $($fields['Package']) is not fully installed: $($fields['Status'])"
    }

    $name = $fields["Package"]
    if ($resolved.ContainsKey($name)) {
        throw "Duplicate installed package record: $name"
    }

    $version = $fields["Version"]
    if ($fields.ContainsKey("Port-Version")) {
        $version = "$version#$($fields["Port-Version"])"
    }
    $resolved[$name] = $version
}

$expected = @{}
foreach ($package in $inventory.packages) {
    if ($expected.ContainsKey($package.name)) {
        throw "Duplicate package in audited inventory: $($package.name)"
    }
    $expected[$package.name] = $package.version
}

$errors = [System.Collections.Generic.List[string]]::new()
foreach ($name in $expected.Keys) {
    if (-not $resolved.ContainsKey($name)) {
        $errors.Add("missing package $name $($expected[$name])")
    } elseif ($resolved[$name] -ne $expected[$name]) {
        $errors.Add("version mismatch for ${name}: expected $($expected[$name]), resolved $($resolved[$name])")
    }
}
foreach ($name in $resolved.Keys) {
    if (-not $expected.ContainsKey($name)) {
        $errors.Add("unexpected package $name $($resolved[$name])")
    }
}
if ($errors.Count -ne 0) {
    $details = $errors -join ([Environment]::NewLine + " - ")
    throw "Resolved dependency inventory mismatch:$([Environment]::NewLine) - $details"
}

$shareRoot = Join-Path (Join-Path $installedRootPath $inventory.triplet) "share"
$copyrightFiles = @{}
foreach ($name in ($expected.Keys | Sort-Object)) {
    $copyrightPath = Require-File -Path (Join-Path $shareRoot "$name/copyright") -Description "Installed copyright material for $name"
    $copyrightFiles[$name] = $copyrightPath
}

$ipoptBuildRecord = Require-File -Path (Join-Path $shareRoot "coin-or-ipopt/upstream-binary-build.txt") -Description "Ipopt upstream binary-build inventory"

$vtkAuxiliaryLicenses = Join-Path $shareRoot "vtk/licenses"
if (-not (Test-Path -LiteralPath $vtkAuxiliaryLicenses -PathType Container) -or
    @(Get-ChildItem -LiteralPath $vtkAuxiliaryLicenses -Recurse -File).Count -eq 0) {
    throw "VTK auxiliary module licenses are missing or empty: $vtkAuxiliaryLicenses"
}
$ipoptUpstreamDocuments = Join-Path $shareRoot "coin-or-ipopt/upstream-doc"
if (-not (Test-Path -LiteralPath $ipoptUpstreamDocuments -PathType Container) -or
    @(Get-ChildItem -LiteralPath $ipoptUpstreamDocuments -Recurse -File).Count -eq 0) {
    throw "Ipopt upstream documentation is missing or empty: $ipoptUpstreamDocuments"
}

if ($BundleOutput) {
    $bundlePath = [IO.Path]::GetFullPath((Join-Path (Get-Location) $BundleOutput))
    if (Test-Path -LiteralPath $bundlePath) {
        throw "Notice bundle output already exists: $bundlePath"
    }

    $licenseOutput = Join-Path $bundlePath "licenses"
    $thirdPartyOutput = Join-Path $bundlePath "thirdparty"
    $thirdPartyLicenseOutput = Join-Path $thirdPartyOutput "LICENSES"
    $documentationOutput = Join-Path $bundlePath "docs"
    $adrOutput = Join-Path $documentationOutput "adr"
    New-Item -ItemType Directory -Path $licenseOutput | Out-Null
    New-Item -ItemType Directory -Path $thirdPartyLicenseOutput | Out-Null
    New-Item -ItemType Directory -Path $adrOutput | Out-Null
    Copy-Item -LiteralPath $projectLicense -Destination (Join-Path $bundlePath "LICENSE")
    Copy-Item -LiteralPath $thirdPartyNotice -Destination $bundlePath
    Copy-Item -LiteralPath $dependencyRecord -Destination (Join-Path $thirdPartyOutput "DEPENDENCIES.md")
    Copy-Item -LiteralPath $releaseChecklist -Destination (Join-Path $documentationOutput "RELEASE_CHECKLIST.md")
    Copy-Item -LiteralPath $inventoryPath -Destination (Join-Path $thirdPartyOutput "vcpkg-resolved-x64-windows.json")
    Copy-Item -LiteralPath $vcpkgOverlayLicense -Destination (Join-Path $thirdPartyLicenseOutput "vcpkg-MIT.txt")
    Copy-Item -LiteralPath $vtkOverlayLicense -Destination (Join-Path $thirdPartyLicenseOutput "vtk-BSD-3-Clause.txt")
    Copy-Item -LiteralPath $tetgenAdr -Destination (Join-Path $adrOutput "0009-tetgen-1-6-agpl-overlay-and-adapter.md")
    Copy-Item -LiteralPath $ipoptAdr -Destination (Join-Path $adrOutput "0010-ipopt-3-14-19-official-windows-binary-adapter.md")
    Copy-Item -LiteralPath $vtkAuxiliaryLicenses -Recurse -Destination (Join-Path $licenseOutput "vtk-module-licenses")
    Copy-Item -LiteralPath $ipoptUpstreamDocuments -Recurse -Destination (Join-Path $licenseOutput "coin-or-ipopt-upstream-doc")

    foreach ($name in ($expected.Keys | Sort-Object)) {
        $safeVersion = $expected[$name] -replace "[^A-Za-z0-9._-]", "_"
        $destination = Join-Path $licenseOutput "$name-$safeVersion-copyright.txt"
        Copy-Item -LiteralPath $copyrightFiles[$name] -Destination $destination
    }
    Copy-Item -LiteralPath $ipoptBuildRecord -Destination (Join-Path $licenseOutput "coin-or-ipopt-upstream-binary-build.txt")

    Write-Host "Staged the audited notice bundle at $bundlePath"
}

Write-Host "Audited $($expected.Count) exact x64-windows packages and their installed copyright files."
Write-Host "This audit does not clear the ADR-0009 or ADR-0010 public binary/hosted-service gates."
