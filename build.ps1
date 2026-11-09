<#
.SYNOPSIS
    Automated build script for Solitaire Engine -- Procedural Hardwood Solitaire 3.0 x Eatscharted.
#>

[CmdletBinding()]
param (
    [switch]$Clean,
    [ValidateSet("Release", "Debug", "RelWithDebInfo")]
    [string]$Config = "Release",
    [switch]$RenderPreview,
    [switch]$Run,
    [switch]$NoLaunch,
    [switch]$NoRun
)

$ErrorActionPreference = "Stop"
Set-Location $PSScriptRoot

$buildTimer = [System.Diagnostics.Stopwatch]::StartNew()

Write-Host "==========================================================================================" -ForegroundColor Cyan
Write-Host "  Solitaire Engine -- Procedural Solitaire Engine Build Script" -ForegroundColor Cyan
Write-Host "==========================================================================================" -ForegroundColor Cyan

if ($Clean -and (Test-Path "build")) {
    Write-Host "[SolEngine] Cleaning build directory..." -ForegroundColor Yellow
    Remove-Item -Recurse -Force "build"
}

# Stop any running game instance so the linker can write the executable
$runningGame = Get-Process -Name "solengine" -ErrorAction SilentlyContinue
if ($runningGame) {
    Write-Host "[SolEngine] Stopping running game instance to allow binary overwrite..." -ForegroundColor Yellow
    $runningGame | Stop-Process -Force -ErrorAction SilentlyContinue
    Start-Sleep -Milliseconds 250
}

cmake -B build -S . -DBUILD_TESTING=OFF
if ($LASTEXITCODE -ne 0) { throw "CMake configuration failed." }

cmake --build build --config $Config --target solengine --parallel
if ($LASTEXITCODE -ne 0) { throw "CMake build failed." }

$exeDir = Join-Path $PSScriptRoot "build\$Config"
if (-not (Test-Path (Join-Path $exeDir "solengine.exe"))) {
    $exeDir = Join-Path $PSScriptRoot "build"
}
$exePath = Join-Path $exeDir "solengine.exe"

$buildTimer.Stop()
$elapsed = $buildTimer.Elapsed
$formattedTime = "{0:D2}m {1:D2}.{2:D3}s" -f $elapsed.Minutes, $elapsed.Seconds, $elapsed.Milliseconds
$totalSec = [Math]::Round($elapsed.TotalSeconds, 2)

Write-Host ("[SolEngine] Built successfully in {0} ({1}s): {2}" -f $formattedTime, $totalSec, $exePath) -ForegroundColor Green

if (Test-Path $exePath) {
    $exeItem = Get-Item $exePath
    $sizeMB = [Math]::Round($exeItem.Length / 1MB, 2)
    $sizeKB = [Math]::Round($exeItem.Length / 1KB, 1)
    $rawBytes = $exeItem.Length
    Write-Host ("[SolEngine] Binary Size: {0} MB ({1} KB / {2} bytes)" -f $sizeMB, $sizeKB, $rawBytes) -ForegroundColor Cyan
}

$wgpuDll = Join-Path $exeDir "wgpu_native.dll"
if (Test-Path $wgpuDll) {
    $dllItem = Get-Item $wgpuDll
    $dllMB = [Math]::Round($dllItem.Length / 1MB, 2)
    $dllBytes = $dllItem.Length
    Write-Host ("[SolEngine] WebGPU Library Size: {0} MB ({1} bytes)" -f $dllMB, $dllBytes) -ForegroundColor Cyan
}

if ($NoRun -or $NoLaunch) {
    Write-Host "[SolEngine] -NoRun / -NoLaunch specified; skipping execution." -ForegroundColor Gray
    exit 0
}

if ($RenderPreview) {
    Write-Host "[SolEngine] Rendering preview snapshot..." -ForegroundColor Cyan
    & $exePath --render-png solengine_preview.png
    exit $LASTEXITCODE
}

Write-Host ("[SolEngine] Launching compiled application: {0}" -f $exePath) -ForegroundColor Green
Start-Process -FilePath $exePath -WorkingDirectory $exeDir
