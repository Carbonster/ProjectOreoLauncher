# install_to_game.ps1 - copies a built ProjectOreo into the game folder.
#
# Installs ONLY the folder:
#     <No Man's Sky>\Binaries\ProjectOreo\
#
# The loader itself (Binaries\version.dll) is NOT touched by default: it is installed by the
# launcher's own Install button on the "ProjectOreo Loader" row - that is exactly the behaviour
# this is meant to test. Pass -WithLoader to install it right away instead.
#
# Usage:
#   powershell -ExecutionPolicy Bypass -File tools\install_to_game.ps1
#   powershell -ExecutionPolicy Bypass -File tools\install_to_game.ps1 -GamePath "D:\Games\steamapps\common\No Man's Sky" -WithLoader

[CmdletBinding()]
param(
    [string]$GamePath,
    [switch]$WithLoader
)

$ErrorActionPreference = "Stop"

$root = Resolve-Path (Join-Path $PSScriptRoot "..")
$dist = Join-Path $root "dist\ProjectOreo"
if (-not (Test-Path (Join-Path $dist "ProjectOreoLauncher.exe"))) {
    throw "nothing staged yet - run tools\stage.ps1 -Build first"
}

# Find the game: parameter -> Steam from the registry -> libraryfolders.vdf
function Find-Game {
    $steam = (Get-ItemProperty -Path "HKCU:\Software\Valve\Steam" -Name SteamPath -ErrorAction SilentlyContinue).SteamPath
    if (-not $steam) { return $null }
    $steam = $steam -replace '/', '\'
    $roots = @($steam)
    foreach ($rel in @("steamapps\libraryfolders.vdf", "config\libraryfolders.vdf")) {
        $vdf = Join-Path $steam $rel
        if (Test-Path $vdf) {
            foreach ($m in [regex]::Matches((Get-Content $vdf -Raw), '"path"\s+"([^"]+)"')) {
                $roots += ($m.Groups[1].Value -replace '\\\\', '\')
            }
        }
    }
    foreach ($r in $roots) {
        $candidate = Join-Path $r "steamapps\common\No Man's Sky"
        if (Test-Path (Join-Path $candidate "Binaries\NMS.exe")) { return $candidate }
    }
    return $null
}

if (-not $GamePath) { $GamePath = Find-Game }
if (-not $GamePath -or -not (Test-Path (Join-Path $GamePath "Binaries\NMS.exe"))) {
    throw "No Man's Sky not found. Pass -GamePath ""...\No Man's Sky"""
}

$binaries = Join-Path $GamePath "Binaries"
$target = Join-Path $binaries "ProjectOreo"
Write-Host "Game     : $GamePath"
Write-Host "Target   : $target"

if (Get-Process -Name "NMS" -ErrorAction SilentlyContinue) {
    throw "No Man's Sky is running - close it first (files would be locked)."
}

New-Item -ItemType Directory -Force -Path $target | Out-Null
# config.ini/logs/cache stay where they are: only what was just built gets copied.
Copy-Item (Join-Path $dist "ProjectOreoLauncher.exe") $target -Force
New-Item -ItemType Directory -Force -Path (Join-Path $target "loader") | Out-Null
Copy-Item (Join-Path $dist "loader\version.dll") (Join-Path $target "loader") -Force

if (Test-Path (Join-Path $dist "python\python.exe")) {
    Write-Host "  copying the private python runtime (this takes a moment)..."
    Copy-Item (Join-Path $dist "python") $target -Recurse -Force
} else {
    Write-Host "  python not staged - the launcher will report it as missing (expected until you fetch it)"
}

if ($WithLoader) {
    $installed = Join-Path $binaries "version.dll"
    if (Test-Path $installed) {
        $product = (Get-Item $installed).VersionInfo.ProductName
        if ($product -ne "ProjectOreo Loader") {
            throw "Binaries\version.dll already belongs to '$product' - resolve that first."
        }
    }
    Copy-Item (Join-Path $dist "loader\version.dll") $installed -Force
    Write-Host "  loader installed: $installed"
} else {
    Write-Host "  loader NOT installed - use the Install button in the launcher (that is the flow to test)"
}

Write-Host "Done. Run: `"$target\ProjectOreoLauncher.exe`" --check-only   (checks only, no window, no launch)"
