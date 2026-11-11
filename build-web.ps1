<#
.SYNOPSIS
    Automated WebAssembly & WebGPU build script for Solitaire Engine.

.DESCRIPTION
    Configures and compiles Solitaire Engine into WebAssembly + WebGPU (WGSL) using Emscripten.
    Optionally serves the built application locally with required COOP/COEP HTTP headers.

.PARAMETER Clean
    Cleans previous web build artifacts before configuring.

.PARAMETER NoServe
    Compiles only, skips starting the local dev server and browser launch.

.PARAMETER Serve
    Explicitly starts the local server.

.PARAMETER Port
    Port for the local server (default: 8080).

.PARAMETER BootstrapEmsdk
    Automatically activates Emscripten SDK if not found in PATH.

.EXAMPLE
    .\build-web.ps1
    .\build-web.ps1 -NoServe
    .\build-web.ps1 -Port 9000
#>

[CmdletBinding()]
param (
    [switch]$Clean,
    [switch]$NoServe,
    [switch]$Serve,
    [int]$Port = 8080,
    [switch]$BootstrapEmsdk
)

$ErrorActionPreference = "Stop"

$rootDir = $PSScriptRoot
if (-not $rootDir) {
    $rootDir = Get-Location
}
Set-Location $rootDir

Write-Host "============================================================" -ForegroundColor Cyan
Write-Host "  Solitaire Engine WebAssembly & WebGPU Build Pipeline   " -ForegroundColor Cyan
Write-Host "============================================================" -ForegroundColor Cyan

function Stop-ExistingWebServer {
    param([int]$TargetPort = 8080)
    $found = $false
    try {
        $connections = Get-NetTCPConnection -LocalPort $TargetPort -ErrorAction SilentlyContinue
        if ($connections) {
            $pidsToKill = $connections | Select-Object -ExpandProperty OwningProcess -Unique | Where-Object { $_ -gt 0 -and $_ -ne $PID }
            foreach ($p in $pidsToKill) {
                Stop-Process -Id $p -Force -ErrorAction SilentlyContinue
                $found = $true
            }
        }
    } catch {}

    try {
        $serveProcesses = Get-CimInstance Win32_Process -Filter "CommandLine LIKE '%serve.py%'" -ErrorAction SilentlyContinue
        if ($serveProcesses) {
            foreach ($proc in $serveProcesses) {
                if ($proc.ProcessId -ne $PID) {
                    Stop-Process -Id $proc.ProcessId -Force -ErrorAction SilentlyContinue
                    $found = $true
                }
            }
        }
    } catch {}

    if ($found) {
        Start-Sleep -Milliseconds 300
    }
}

if (-not $NoServe) {
    Stop-ExistingWebServer -TargetPort $Port
}

# ------------------------------------------------------------------
# STEP 1: Verify Emscripten Toolchain
# ------------------------------------------------------------------
Write-Host "`n[*] STEP 1: Verifying Emscripten SDK environment..." -ForegroundColor Yellow

if (-not (Get-Command "emcc" -ErrorAction SilentlyContinue)) {
    # Check parent or sibling emsdk installations (e.g. in eatsbits)
    $candidateEmsdk = @(
        (Join-Path $rootDir "emsdk"),
        "c:\git\eatsbits\emsdk",
        "$env:USERPROFILE\emsdk",
        "C:\emsdk"
    )
    foreach ($cand in $candidateEmsdk) {
        $envScript = Join-Path $cand "emsdk_env.ps1"
        if (Test-Path $envScript) {
            Write-Host "    Found EMSDK at '$cand'. Activating..." -ForegroundColor Gray
            & $envScript | Out-Null
            break
        }
    }
}

if (-not (Get-Command "emcc" -ErrorAction SilentlyContinue)) {
    Write-Error "Emscripten SDK (emcc/emcmake) not found in PATH or standard directories. Please install or activate EMSDK."
    exit 1
}

$emccVer = (emcc --version)[0]
Write-Host "    Found Emscripten: $emccVer" -ForegroundColor Green

# ------------------------------------------------------------------
# STEP 2: Configure & Build Web Target
# ------------------------------------------------------------------
$buildDir = Join-Path $rootDir "build-web"

if ($Clean -and (Test-Path $buildDir)) {
    Write-Host "`n[*] Cleaning previous web build directory ('$buildDir')..." -ForegroundColor Yellow
    Remove-Item -Recurse -Force $buildDir
}

Write-Host "`n[*] STEP 2: Configuring CMake with Emscripten..." -ForegroundColor Yellow
if (-not (Test-Path $buildDir)) {
    New-Item -ItemType Directory -Path $buildDir | Out-Null
}

try {
    $ninjaDir = python -c "import ninja; print(ninja.BIN_DIR)" 2>$null
    if ($ninjaDir -and (Test-Path $ninjaDir)) {
        $env:PATH = "$ninjaDir;$env:PATH"
    }
    $userBase = python -m site --user-base 2>$null
    if ($userBase) {
        $userScripts = Join-Path $userBase "Scripts"
        if (Test-Path $userScripts) {
            $env:PATH = "$userScripts;$env:PATH"
        }
    }
} catch {}

emcmake cmake -B build-web -S . -G Ninja -DCMAKE_BUILD_TYPE=Release
if ($LASTEXITCODE -ne 0) {
    Write-Error "CMake configuration failed."
    exit $LASTEXITCODE
}

Write-Host "`n[*] STEP 3: Compiling solengine_web (WebGPU + WASM)..." -ForegroundColor Yellow
cmake --build build-web --target solengine_web --config Release
if ($LASTEXITCODE -ne 0) {
    Write-Error "Compilation failed."
    exit $LASTEXITCODE
}

Write-Host "`n[+] Build successful!" -ForegroundColor Green

# Stage on-demand decks and themes (served statically alongside WASM without payload bloat)
$webDecksDir = Join-Path $buildDir "decks"
if (-not (Test-Path $webDecksDir)) {
    New-Item -ItemType Directory -Path $webDecksDir -Force | Out-Null
}
Get-ChildItem -Path (Join-Path $rootDir "decks") | ForEach-Object {
    Copy-Item -Path $_.FullName -Destination (Join-Path $webDecksDir $_.Name) -Force
}

$webThemesDir = Join-Path $buildDir "themes"
if (-not (Test-Path $webThemesDir)) {
    New-Item -ItemType Directory -Path $webThemesDir -Force | Out-Null
}
Get-ChildItem -Path (Join-Path $rootDir "themes") | ForEach-Object {
    Copy-Item -Path $_.FullName -Destination (Join-Path $webThemesDir $_.Name) -Force
}

$webGamesDir = Join-Path $buildDir "games"
if (-not (Test-Path $webGamesDir)) {
    New-Item -ItemType Directory -Path $webGamesDir -Force | Out-Null
}
Get-ChildItem -Path (Join-Path $rootDir "games") | ForEach-Object {
    Copy-Item -Path $_.FullName -Destination (Join-Path $webGamesDir $_.Name) -Force
}

$webSoundsDir = Join-Path $buildDir "sounds"
if (-not (Test-Path $webSoundsDir)) {
    New-Item -ItemType Directory -Path $webSoundsDir -Force | Out-Null
}
Get-ChildItem -Path (Join-Path $rootDir "sounds") | ForEach-Object {
    Copy-Item -Path $_.FullName -Destination (Join-Path $webSoundsDir $_.Name) -Force
}

# Also stage directly to /web/ so static hosts / dev servers pointing to /web/ have immediate access
$staticWebDir = Join-Path $rootDir "web"
if (Test-Path $staticWebDir) {
    $staticDecks = Join-Path $staticWebDir "decks"
    if (-not (Test-Path $staticDecks)) { New-Item -ItemType Directory -Path $staticDecks -Force | Out-Null }
    Get-ChildItem -Path (Join-Path $rootDir "decks") | ForEach-Object {
        Copy-Item -Path $_.FullName -Destination (Join-Path $staticDecks $_.Name) -Force
    }
    $staticThemes = Join-Path $staticWebDir "themes"
    if (-not (Test-Path $staticThemes)) { New-Item -ItemType Directory -Path $staticThemes -Force | Out-Null }
    Get-ChildItem -Path (Join-Path $rootDir "themes") | ForEach-Object {
        Copy-Item -Path $_.FullName -Destination (Join-Path $staticThemes $_.Name) -Force
    }
    $staticGames = Join-Path $staticWebDir "games"
    if (-not (Test-Path $staticGames)) { New-Item -ItemType Directory -Path $staticGames -Force | Out-Null }
    Get-ChildItem -Path (Join-Path $rootDir "games") | ForEach-Object {
        Copy-Item -Path $_.FullName -Destination (Join-Path $staticGames $_.Name) -Force
    }
    $staticSounds = Join-Path $staticWebDir "sounds"
    if (-not (Test-Path $staticSounds)) { New-Item -ItemType Directory -Path $staticSounds -Force | Out-Null }
    Get-ChildItem -Path (Join-Path $rootDir "sounds") | ForEach-Object {
        Copy-Item -Path $_.FullName -Destination (Join-Path $staticSounds $_.Name) -Force
    }
}

# ------------------------------------------------------------------
# Payload Analysis (Raw vs Gzip Wire Transfer)
# ------------------------------------------------------------------
Write-Host "`n========================================================================================" -ForegroundColor Cyan
Write-Host "  Solitaire Engine Web Payload Analysis (Wire Footprint)                              " -ForegroundColor Cyan
Write-Host "========================================================================================" -ForegroundColor Cyan

$webArtifacts = @(
    @{ Name = "solengine.wasm"; Desc = "WebAssembly Core Binary" },
    @{ Name = "solengine.data"; Desc = "Preloaded Core Assets (Classic Deck + Rosewood Theme)" },
    @{ Name = "solengine.js";   Desc = "Emscripten JS Runtime Bridge" },
    @{ Name = "index.html";        Desc = "WebGPU Entry & PWA Host" },
    @{ Name = "sw.js";             Desc = "Offline PWA Service Worker (COOP/COEP)" },
    @{ Name = "manifest.json";     Desc = "Web App Manifest" }
)

$totalRaw = 0
$totalWire = 0

foreach ($item in $webArtifacts) {
    $p = Join-Path $buildDir $item.Name
    if (Test-Path $p) {
        $rawBytes = (Get-Item $p).Length
        $totalRaw += $rawBytes

        $rawBuffer = [System.IO.File]::ReadAllBytes($p)
        $ms = New-Object System.IO.MemoryStream
        $gz = New-Object System.IO.Compression.GZipStream($ms, [System.IO.Compression.CompressionMode]::Compress)
        $gz.Write($rawBuffer, 0, $rawBuffer.Length)
        $gz.Close()
        $wireBytes = $ms.ToArray().Length
        $ms.Close()
        $totalWire += $wireBytes

        $savedPct = if ($rawBytes -gt 0) { [math]::Round((1.0 - ($wireBytes / $rawBytes)) * 100.0, 1) } else { 0.0 }
        $rawStr = if ($rawBytes -ge 1MB) { "{0:N2} MB" -f ($rawBytes / 1MB) } else { "{0:N1} KB" -f ($rawBytes / 1KB) }
        $wireStr = if ($wireBytes -ge 1MB) { "{0:N2} MB" -f ($wireBytes / 1MB) } else { "{0:N1} KB" -f ($wireBytes / 1KB) }

        Write-Host ("  {0,-18} {1,10} {2,12} -{3,4}%   {4,-40}" -f $item.Name, $rawStr, $wireStr, $savedPct, $item.Desc) -ForegroundColor Gray
    }
}

Write-Host "  --------------------------------------------------------------------------------------" -ForegroundColor Gray
$totRawStr = if ($totalRaw -ge 1MB) { "{0:N2} MB" -f ($totalRaw / 1MB) } else { "{0:N1} KB" -f ($totalRaw / 1KB) }
$totWireStr = if ($totalWire -ge 1MB) { "{0:N2} MB" -f ($totalWire / 1MB) } else { "{0:N1} KB" -f ($totalWire / 1KB) }
$totSavedPct = if ($totalRaw -gt 0) { [math]::Round((1.0 - ($totalWire / $totalRaw)) * 100.0, 1) } else { 0.0 }
Write-Host ("  {0,-18} {1,10} {2,12} -{3,4}%   {4,-40}" -f "TOTAL PAYLOAD", $totRawStr, $totWireStr, $totSavedPct, "CDN Wire Transfer Footprint") -ForegroundColor Green
Write-Host "========================================================================================`n" -ForegroundColor Cyan

# ------------------------------------------------------------------
# STEP 4: Local Dev Server with COOP / COEP Headers (Optional)
# ------------------------------------------------------------------
if (-not $NoServe) {
    Write-Host "`n[*] STEP 4: Starting local HTTP server on port $Port..." -ForegroundColor Yellow
    Write-Host "    Serving headers for SharedArrayBuffer & WebGPU:" -ForegroundColor Gray
    Write-Host "      Cross-Origin-Opener-Policy: same-origin" -ForegroundColor Gray
    Write-Host "      Cross-Origin-Embedder-Policy: require-corp" -ForegroundColor Gray
    Write-Host "`n    [+] SolEngine Web: http://localhost:$Port/index.html" -ForegroundColor Green

    $serverScript = @"
import sys, os, http.server, socketserver

web_dir = r'$buildDir'
if os.path.exists(web_dir):
    os.chdir(web_dir)

class CustomHandler(http.server.SimpleHTTPRequestHandler):
    def end_headers(self):
        self.send_header('Cross-Origin-Opener-Policy', 'same-origin')
        self.send_header('Cross-Origin-Embedder-Policy', 'require-corp')
        super().end_headers()

Handler = CustomHandler
socketserver.TCPServer.allow_reuse_address = True
with socketserver.TCPServer(('', $Port), Handler) as httpd:
    print('Serving HTTP on 0.0.0.0 port $Port with COOP/COEP headers...', flush=True)
    while True:
        try:
            httpd.serve_forever()
        except KeyboardInterrupt:
            break
        except Exception:
            pass
"@

    $serverScriptPath = Join-Path $buildDir "serve.py"
    Set-Content -Path $serverScriptPath -Value $serverScript

    Write-Host "`n[*] Opening browser to http://localhost:$Port/index.html..." -ForegroundColor Cyan
    Start-Process "http://localhost:$Port/index.html"
    python $serverScriptPath
} else {
    Write-Host "`n[*] Build completed. Server launch skipped (-NoServe specified)." -ForegroundColor Gray
}

