# stage.ps1 - assembles the install-ready ProjectOreo folder from a fresh build.
#
# Downloads nothing. The private Python is picked up from dist\ProjectOreo\python if it is
# already there (it is put there separately by fetch_python.ps1 - run that by hand, on purpose).
#
# Result:
#   dist\ProjectOreo\
#       ProjectOreoLauncher.exe
#       loader\version.dll        <- this is what the launcher installs into Binaries
#       python\                   <- if fetched
#       logs\                     <- created on first run
#       config.ini                <- created on first run
#
# Usage:
#   powershell -ExecutionPolicy Bypass -File tools\stage.ps1
#   powershell -ExecutionPolicy Bypass -File tools\stage.ps1 -Build

[CmdletBinding()]
param(
    [string]$Config = "Release",
    [switch]$Build   # also rebuild through cmake
)

$ErrorActionPreference = "Stop"

$root = Resolve-Path (Join-Path $PSScriptRoot "..")
$buildDir = Join-Path $root "build"
$binDir = Join-Path $buildDir $Config
$dist = Join-Path $root "dist\ProjectOreo"

if ($Build) {
    $cmake = "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
    if (-not (Test-Path $cmake)) { throw "cmake not found at $cmake" }
    & $cmake -S $root -B $buildDir -A x64
    if ($LASTEXITCODE -ne 0) { throw "cmake configure failed" }
    & $cmake --build $buildDir --config $Config
    if ($LASTEXITCODE -ne 0) { throw "cmake build failed" }
}

$exe = Join-Path $binDir "ProjectOreoLauncher.exe"
$dll = Join-Path $binDir "version.dll"
foreach ($f in @($exe, $dll)) {
    if (-not (Test-Path $f)) { throw "missing build output: $f  (run with -Build first)" }
}

New-Item -ItemType Directory -Force -Path $dist | Out-Null
New-Item -ItemType Directory -Force -Path (Join-Path $dist "loader") | Out-Null

Copy-Item $exe (Join-Path $dist "ProjectOreoLauncher.exe") -Force
Copy-Item $dll (Join-Path $dist "loader\version.dll") -Force

Write-Host "Staged into: $dist"
Write-Host ("  launcher : {0}" -f (Get-Item (Join-Path $dist "ProjectOreoLauncher.exe")).Length)
Write-Host ("  loader   : {0}" -f (Get-Item (Join-Path $dist "loader\version.dll")).Length)
if (Test-Path (Join-Path $dist "python\python.exe")) {
    $v = & (Join-Path $dist "python\python.exe") -I -c "import sys;print('%d.%d.%d' % sys.version_info[:3])"
    Write-Host "  python   : $v"
} else {
    Write-Host "  python   : NOT PRESENT - run tools\fetch_python.ps1 when you want it"
}
