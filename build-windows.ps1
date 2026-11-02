# Requires Visual Studio Build Tools (Desktop development with C++) and CMake.
[CmdletBinding()]
param(
    [ValidateSet('Release', 'MinSizeRel', 'Debug')]
    [string]$Configuration = 'MinSizeRel',
    [switch]$Package,
    [string]$Version = ''
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
Set-Location $PSScriptRoot

# Check a release tag before spending time compiling it.
if ($Package) {
    & "$PSScriptRoot/scripts/package.ps1" -Version $Version -ValidateOnly
}
cmake -S . -B build -A x64 -DBUILD_TESTING=ON
if ($LASTEXITCODE -ne 0) { throw 'CMake configure failed' }
cmake --build build --config $Configuration --parallel 2
if ($LASTEXITCODE -ne 0) { throw 'Build failed' }
ctest --test-dir build -C $Configuration --output-on-failure --output-junit test-results.xml
if ($LASTEXITCODE -ne 0) { throw 'Tests failed; packages will not be published' }

if ($Package) {
    & "$PSScriptRoot/scripts/package.ps1" -Version $Version -BinaryPath "build/$Configuration/ImageCompare.exe"
} else {
    New-Item -ItemType Directory -Force dist | Out-Null
    Copy-Item "build/$Configuration/ImageCompare.exe" dist/ImageCompare.exe -Force
}
Write-Host 'Ready: dist/ImageCompare.exe (portable; no installation required)'
