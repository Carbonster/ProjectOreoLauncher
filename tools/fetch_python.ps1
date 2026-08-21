# fetch_python.ps1 - downloads ProjectOreo's private Python runtime.
#
# ProjectOreo never uses the machine's Python. It ships its own, and this script is how that
# copy is produced at build/packaging time. The result is a self contained folder:
#
#     <dest>\python.exe, python313.dll, Lib\, Scripts\, ...
#
# Source: astral-sh/python-build-standalone "install_only_stripped" build - a normal, complete
# CPython (pip and tkinter included), unlike the python.org embeddable zip, but without the
# ~200 .pdb debug-symbol files the plain "install_only" asset carries in DLLs\. Those are
# astral's build symbols, useless to us and to a player, and they cost tens of megabytes in
# every release and every copy. Do not drop the _stripped suffix to "get debugging back": the
# symbols describe CPython's own binaries, not ours.
#
# Usage:
#   powershell -ExecutionPolicy Bypass -File tools\fetch_python.ps1
#   powershell -ExecutionPolicy Bypass -File tools\fetch_python.ps1 -Dest "D:\...\ProjectOreo\python"
#   powershell -ExecutionPolicy Bypass -File tools\fetch_python.ps1 -WithPackages

[CmdletBinding()]
param(
    # Where the runtime ends up. Default: <repo>\dist\ProjectOreo\python
    [string]$Dest = (Join-Path $PSScriptRoot "..\dist\ProjectOreo\python"),

    # Pinned build. ProjectOreo controls its Python version deliberately: pyMHF/NMSpy have
    # compiled dependencies (cyminhook, pymem, pyrun-injected) and 3.13 is what the mod stack
    # is known to work on. Change this on purpose, never by accident.
    [string]$Release = "20260814",
    [string]$PythonVersion = "3.13.15",

    # Also install pyMHF and NMSpy into the fresh runtime.
    [switch]$WithPackages,

    [switch]$Force
)

$ErrorActionPreference = "Stop"
$ProgressPreference = "SilentlyContinue"

$asset = "cpython-$PythonVersion+$Release-x86_64-pc-windows-msvc-install_only_stripped.tar.gz"
$url = "https://github.com/astral-sh/python-build-standalone/releases/download/$Release/$asset"

$Dest = [System.IO.Path]::GetFullPath($Dest)
Write-Host "ProjectOreo private Python"
Write-Host "  version : $PythonVersion (build $Release)"
Write-Host "  target  : $Dest"

if (Test-Path (Join-Path $Dest "python.exe")) {
    if (-not $Force) {
        Write-Host "  already present - use -Force to replace it."
        exit 0
    }
    Write-Host "  removing the existing runtime (-Force)..."
    Remove-Item -Recurse -Force $Dest
}

$work = Join-Path ([System.IO.Path]::GetTempPath()) ("oreo_python_" + [System.Guid]::NewGuid().ToString("N"))
New-Item -ItemType Directory -Force -Path $work | Out-Null
$archive = Join-Path $work $asset

try {
    Write-Host "  downloading $asset ..."
    Invoke-WebRequest -Uri $url -OutFile $archive -UseBasicParsing

    Write-Host "  extracting ..."
    # tar is part of Windows 10 1803+ and handles .tar.gz natively.
    & tar.exe -xzf $archive -C $work
    if ($LASTEXITCODE -ne 0) { throw "tar failed with exit code $LASTEXITCODE" }

    $extracted = Join-Path $work "python"
    if (-not (Test-Path (Join-Path $extracted "python.exe"))) {
        throw "the archive did not contain python\python.exe as expected"
    }

    $parent = Split-Path -Parent $Dest
    if (-not (Test-Path $parent)) { New-Item -ItemType Directory -Force -Path $parent | Out-Null }
    Move-Item -Path $extracted -Destination $Dest

    $python = Join-Path $Dest "python.exe"
    $reported = & $python -I -c "import sys;print('%d.%d.%d' % sys.version_info[:3])"
    Write-Host "  installed  : $reported"
    $pip = & $python -I -m pip --version
    Write-Host "  pip        : $pip"

    if ($WithPackages) {
        Write-Host "  installing pyMHF and NMSpy into the private runtime ..."
        # NMSpy pulls pyMHF in as a dependency; both are installed explicitly anyway so the
        # launcher sees two independently reported versions.
        & $python -I -m pip install --disable-pip-version-check --no-input nmspy pymhf
        if ($LASTEXITCODE -ne 0) { throw "pip install failed with exit code $LASTEXITCODE" }
        & $python -I -c "import importlib.metadata as m;print('  pymhf:', m.version('pymhf'));print('  nmspy:', m.version('nmspy'))"
    }

    Write-Host "Done."
}
finally {
    if (Test-Path $work) { Remove-Item -Recurse -Force $work -ErrorAction SilentlyContinue }
}
