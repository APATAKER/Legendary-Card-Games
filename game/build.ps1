<#
.SYNOPSIS
  Build (and optionally run) the native game with the Visual Studio 2026 Build Tools.

.EXAMPLE
  ./build.ps1               # Debug build
  ./build.ps1 -Run          # Debug build, then launch
  ./build.ps1 Release -Run  # Optimised build, then launch
  ./build.ps1 -Clean        # wipe build/ first
#>
param(
    [ValidateSet('Debug', 'Release')] [string] $Config = 'Debug',
    [switch] $Run,
    [switch] $Clean
)
$ErrorActionPreference = 'Stop'
Set-Location $PSScriptRoot

# --- enter an x64 MSVC developer environment (cl, ninja, cmake, vcpkg on PATH) ---
if (-not (Get-Command cl.exe -ErrorAction SilentlyContinue)) {
    $vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
    $vs = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    if (-not $vs) { throw 'No Visual Studio / Build Tools installation with the C++ x64 toolset was found.' }
    Import-Module "$vs\Common7\Tools\Microsoft.VisualStudio.DevShell.dll"
    Enter-VsDevShell -VsInstallPath $vs -SkipAutomaticLocation -DevCmdArguments '-arch=x64 -host_arch=x64' | Out-Null
    if (-not $env:VCPKG_ROOT) { $env:VCPKG_ROOT = "$vs\VC\vcpkg" }
}
Set-Location $PSScriptRoot

if ($Clean) { Remove-Item -Recurse -Force build -ErrorAction SilentlyContinue }

# --- generated assets (board, card textures, layout) ---
if (-not (Test-Path assets/board.png) -or -not (Test-Path assets/catalog.json)) {
    Write-Host '==> Building assets' -ForegroundColor Cyan
    py ../tools/build_assets.py
    if ($LASTEXITCODE) { throw 'asset pipeline failed' }
}

Write-Host "==> Configure" -ForegroundColor Cyan
cmake --preset windows-x64
if ($LASTEXITCODE) { throw 'configure failed' }

Write-Host "==> Build ($Config)" -ForegroundColor Cyan
cmake --build --preset $Config.ToLower()
if ($LASTEXITCODE) { throw 'build failed' }

$exe = "build\$Config\LegendaryEncounters.exe"
Write-Host "==> $exe" -ForegroundColor Green
if ($Run) { & $exe }
