[CmdletBinding()]
param(
    [string]$BinaryPath = 'build/MinSizeRel/ImageCompare.exe',
    [string]$Version = '',
    [switch]$ValidateOnly,
    [string]$OutputDirectory = 'dist'
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$root = Split-Path $PSScriptRoot -Parent
Set-Location $root

$cmake = Get-Content (Join-Path $root 'CMakeLists.txt') -Raw
$match = [regex]::Match($cmake, 'project\(ImageCompare VERSION (\d+\.\d+\.\d+)')
if (-not $match.Success) { throw 'Cannot read the CMake project version' }
$projectVersion = $match.Groups[1].Value
if ([string]::IsNullOrWhiteSpace($Version)) { $Version = "v$projectVersion" }
if ($Version -notmatch '^v(0|[1-9]\d*)\.(0|[1-9]\d*)\.(0|[1-9]\d*)(-[0-9A-Za-z-]+(\.[0-9A-Za-z-]+)*)?$') {
    throw "Invalid version '$Version'; use vMAJOR.MINOR.PATCH or vMAJOR.MINOR.PATCH-rc.1"
}
if (($Version.Substring(1) -split '-', 2)[0] -ne $projectVersion) {
    throw "Tag $Version does not match CMake project version $projectVersion"
}
$resource = Get-Content (Join-Path $root 'assets/app.rc') -Raw
$resourceVersion = [regex]::Match($resource, 'FILEVERSION\s+(\d+),(\d+),(\d+),(\d+)')
if (-not $resourceVersion.Success) { throw 'Cannot read the Windows resource version' }
$numericResourceVersion = ($resourceVersion.Groups[1..3] | ForEach-Object { $_.Value }) -join '.'
if ($numericResourceVersion -ne $projectVersion) {
    throw "Windows resource version $numericResourceVersion does not match $projectVersion"
}
if ($ValidateOnly) { Write-Host "Version validated: $Version"; return }

$binary = (Resolve-Path $BinaryPath).Path
$output = [IO.Path]::GetFullPath((Join-Path $root $OutputDirectory))
New-Item -ItemType Directory -Path $output -Force | Out-Null
# Never put old packages from a previous local build in a new release artifact.
$stage = Join-Path ([IO.Path]::GetTempPath()) ('imagecompare-package-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $stage | Out-Null
try {
    $commit = git rev-parse HEAD
    if ($LASTEXITCODE -ne 0) { throw 'Cannot determine the source commit' }
    $runUrl = ''
    if ($env:GITHUB_SERVER_URL -and $env:GITHUB_REPOSITORY -and $env:GITHUB_RUN_ID) {
        $runUrl = "$env:GITHUB_SERVER_URL/$env:GITHUB_REPOSITORY/actions/runs/$env:GITHUB_RUN_ID"
    }
    $info = @(
        'ImageCompare portable Windows x64',
        "Version: $Version",
        "Commit: $commit",
        "Built at (UTC): $([DateTime]::UtcNow.ToString('yyyy-MM-ddTHH:mm:ssZ'))",
        "Build run: $runUrl",
        'Runtime: Windows system DLLs; static C/C++ runtime',
        'Extract ImageCompare.exe and run it; no installer is required.',
        'See the linked CI run for native Windows build and test results.'
    )
    $info | Set-Content (Join-Path $stage 'BUILD-INFO.txt') -Encoding utf8
    Copy-Item $binary (Join-Path $stage 'ImageCompare.exe')
    Copy-Item (Join-Path $root 'README.txt') (Join-Path $stage 'README.txt')
    Copy-Item (Join-Path $root 'VALIDATION.txt') (Join-Path $stage 'VALIDATION.txt')
    $portable = Join-Path $stage "ImageCompare-$Version-windows-x64.zip"
    Compress-Archive -LiteralPath @(
        (Join-Path $stage 'ImageCompare.exe'), (Join-Path $stage 'README.txt'),
        (Join-Path $stage 'VALIDATION.txt'), (Join-Path $stage 'BUILD-INFO.txt')
    ) -DestinationPath $portable -CompressionLevel Optimal
    $source = Join-Path $stage "ImageCompare-$Version-source.zip"
    git archive --format=zip "--prefix=ImageCompare-$Version/" -o $source HEAD
    if ($LASTEXITCODE -ne 0) { throw 'Source archive failed' }
    $assets = @((Join-Path $stage 'ImageCompare.exe'), $portable, $source, (Join-Path $stage 'BUILD-INFO.txt'))
    $lines = foreach ($asset in $assets) {
        $hash = (Get-FileHash -LiteralPath $asset -Algorithm SHA256).Hash.ToLowerInvariant()
        "$hash  $([IO.Path]::GetFileName($asset))"
    }
    [IO.File]::WriteAllText((Join-Path $stage 'SHA256SUMS.txt'), (($lines -join "`n") + "`n"), [Text.Encoding]::ASCII)
    # The output directory belongs to this script; clean only our own package names.
    Get-ChildItem -LiteralPath $output -File | Where-Object {
        $_.Name -eq 'ImageCompare.exe' -or $_.Name -eq 'SHA256SUMS.txt' -or
        $_.Name -eq 'BUILD-INFO.txt' -or $_.Name -like 'ImageCompare-*-windows-x64.zip' -or
        $_.Name -like 'ImageCompare-*-source.zip'
    } | Remove-Item -Force
    foreach ($asset in ($assets + @((Join-Path $stage 'SHA256SUMS.txt')))) {
        Copy-Item -LiteralPath $asset -Destination $output
    }
    Write-Host "Packaged $Version in $output"
} finally {
    Remove-Item -LiteralPath $stage -Recurse -Force
}
