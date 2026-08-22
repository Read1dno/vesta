-- Vesta Web Radar single-file distribution. Generated; edit vesta_web_radar sources.

-- Copy only this file into %TEMP%\vesta\lua\scripts, refresh, then enable it.

__VESTA_WEB_RADAR_HELPER = [====[param(
    [Parameter(Mandatory=$true)][string]$Root,
    [Parameter(Mandatory=$true)][string]$Runtime,
    [Parameter(Mandatory=$true)][string]$Session,
    [string]$HtmlFile = '',
    [switch]$NoTunnel
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
try { [Diagnostics.Process]::GetCurrentProcess().PriorityClass = 'BelowNormal' } catch { }
if ($Session -notmatch '^[A-Za-z0-9-]{1,96}$') { throw 'invalid session id' }
$statusFile = Join-Path $Runtime "$Session-status.json"
$stateFiles = @((Join-Path $Runtime "$Session-state-a.json"),(Join-Path $Runtime "$Session-state-b.json"))
$mapMetaFile = Join-Path $Runtime "$Session-map-meta.json"
$stopFile = Join-Path $Runtime "$Session-stop.flag"
$htmlFile = if ([string]::IsNullOrWhiteSpace($HtmlFile)) {
    Join-Path (Join-Path $Root 'public') 'index.html'
} else {
    [IO.Path]::GetFullPath($HtmlFile)
}
$weaponFontFile = Join-Path $Runtime "$Session-weapon-icons.ttf"
$tunnelOut = Join-Path $Runtime "$Session-tunnel.out.log"
$tunnelErr = Join-Path $Runtime "$Session-tunnel.err.log"
$cloudflaredVersion = '2026.8.1'
$cloudflaredSha256 = '8f1d6f87b8756dbf37064b16e2c8251b69d816305e4f4373e1b80efb28d13b83'
$cloudflaredUrl = "https://github.com/cloudflare/cloudflared/releases/download/$cloudflaredVersion/cloudflared-windows-amd64.exe"
$cloudflaredDirectory = Join-Path $Runtime "modules\cloudflared\$cloudflaredVersion"
$cloudflaredExe = Join-Path $cloudflaredDirectory 'cloudflared.exe'
$cloudflaredPart = Join-Path $cloudflaredDirectory 'cloudflared.exe.part'
$cloudflaredConfig = Join-Path $cloudflaredDirectory 'quick-tunnel.yml'
$listener = $null
$tunnel = $null
$tunnelOutput = $null
$tunnelError = $null
$tunnelLog = ''
$failed = $false
$tunnelRestartAt = [DateTime]::MaxValue
$tunnelRestartDelaySeconds = 1
$cloudflaredReady = $false

function Write-Status([string]$Phase, [string]$LocalUrl, [string]$PublicUrl, [string]$Message) {
    $payload = [ordered]@{ status=$Phase; local_url=$LocalUrl; public_url=$PublicUrl; error=$Message; updated=(Get-Date).ToUniversalTime().ToString('o') }
    $temporary = "$statusFile.tmp"
    $backup = "$statusFile.bak"
    $json = $payload | ConvertTo-Json -Compress
    for ($attempt = 0; $attempt -lt 20; $attempt++) {
        try {
            [IO.File]::WriteAllText($temporary, $json, [Text.UTF8Encoding]::new($false))
            if (Test-Path -LiteralPath $statusFile) {
                Remove-Item -LiteralPath $backup -Force -ErrorAction SilentlyContinue
                [IO.File]::Replace($temporary, $statusFile, $backup, $true)
                Remove-Item -LiteralPath $backup -Force -ErrorAction SilentlyContinue
            } else {
                [IO.File]::Move($temporary, $statusFile)
            }
            return
        } catch {
            if ($attempt -eq 19) { throw }
            Start-Sleep -Milliseconds 10
        }
    }
}

function Read-SharedText([string]$Path) {
    if (-not (Test-Path -LiteralPath $Path)) { return '' }
    $stream = [IO.FileStream]::new($Path, [IO.FileMode]::Open, [IO.FileAccess]::Read, [IO.FileShare]::ReadWrite)
    try {
        $reader = [IO.StreamReader]::new($stream, [Text.Encoding]::UTF8, $true, 1024, $true)
        try { return $reader.ReadToEnd() } finally { $reader.Dispose() }
    } finally { $stream.Dispose() }
}

function Read-SharedBytes([string]$Path) {
    if (-not (Test-Path -LiteralPath $Path)) { return [byte[]]::new(0) }
    $stream = [IO.FileStream]::new($Path, [IO.FileMode]::Open, [IO.FileAccess]::Read, [IO.FileShare]::ReadWrite)
    try {
        $buffer = [byte[]]::new($stream.Length)
        $offset = 0
        while ($offset -lt $buffer.Length) {
            $read = $stream.Read($buffer, $offset, $buffer.Length - $offset)
            if ($read -le 0) { break }
            $offset += $read
        }
        if ($offset -eq $buffer.Length) { return $buffer }
        $trimmed = [byte[]]::new($offset)
        [Array]::Copy($buffer, $trimmed, $offset)
        return $trimmed
    } finally { $stream.Dispose() }
}

function Test-Cloudflared([string]$Path) {
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) { return $false }
    try {
        $actual = (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant()
        return $actual -eq $cloudflaredSha256
    } catch { return $false }
}

function Install-Cloudflared([string]$LocalUrl) {
    if (Test-Cloudflared $cloudflaredExe) { return $true }
    New-Item -ItemType Directory -Path $cloudflaredDirectory -Force | Out-Null
    Remove-Item -LiteralPath $cloudflaredExe,$cloudflaredPart -Force -ErrorAction SilentlyContinue
    Write-Status 'downloading-cloudflared' $LocalUrl '' "Cloudflare Tunnel $cloudflaredVersion"
    try {
        [Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12
        $client = [Net.WebClient]::new()
        try {
            $client.Headers.Add([Net.HttpRequestHeader]::UserAgent, 'Vesta-Web-Radar/1.0')
            $client.DownloadFile([Uri]$cloudflaredUrl, $cloudflaredPart)
        } finally { $client.Dispose() }
        Write-Status 'verifying-cloudflared' $LocalUrl '' "Cloudflare Tunnel $cloudflaredVersion"
        if (-not (Test-Cloudflared $cloudflaredPart)) {
            throw 'downloaded cloudflared failed SHA-256 verification'
        }
        Move-Item -LiteralPath $cloudflaredPart -Destination $cloudflaredExe -Force
        return $true
    } catch {
        Remove-Item -LiteralPath $cloudflaredPart -Force -ErrorAction SilentlyContinue
        Write-Status 'local-only' $LocalUrl '' ("cloudflared download failed: " + $_.Exception.Message)
        return $false
    }
}

function Start-CloudflareTunnel([int]$Port, [string]$LocalUrl) {
    Remove-Item -LiteralPath $tunnelOut,$tunnelErr -Force -ErrorAction SilentlyContinue
    # Never inherit ~/.cloudflared/config.yml. A named-tunnel ingress config can
    # accept the Quick Tunnel connection but answer every public request with
    # its catch-all 404 route.
    [IO.File]::WriteAllText($cloudflaredConfig, "no-autoupdate: true`n", [Text.UTF8Encoding]::new($false))
    $processInfo = [Diagnostics.ProcessStartInfo]::new()
    $processInfo.FileName = $cloudflaredExe
    $processInfo.Arguments = "--config `"$cloudflaredConfig`" --no-autoupdate tunnel --url http://127.0.0.1:$Port"
    $processInfo.UseShellExecute = $false
    $processInfo.CreateNoWindow = $true
    $processInfo.RedirectStandardOutput = $true
    $processInfo.RedirectStandardError = $true
    $script:tunnel = [Diagnostics.Process]::Start($processInfo)
    try { $script:tunnel.PriorityClass = 'BelowNormal' } catch { }
    $script:tunnelOutput = $script:tunnel.StandardOutput.ReadLineAsync()
    $script:tunnelError = $script:tunnel.StandardError.ReadLineAsync()
    $script:tunnelLog = ''
    $script:tunnelRestartAt = [DateTime]::MaxValue
    Write-Status 'opening-tunnel' $LocalUrl '' ''
}

function Send-Response($Client, [int]$Code, [string]$Reason, [string]$Type, [byte[]]$Body,
    [string]$Cache = 'no-store, max-age=0') {
    $stream = $Client.GetStream()
    $header = "HTTP/1.1 $Code $Reason`r`nContent-Type: $Type`r`nContent-Length: $($Body.Length)`r`nCache-Control: $Cache`r`nPragma: no-cache`r`nX-Content-Type-Options: nosniff`r`nReferrer-Policy: no-referrer`r`nContent-Security-Policy: default-src 'self'; style-src 'self' 'unsafe-inline'; script-src 'self' 'unsafe-inline'; connect-src 'self'; img-src 'self' data:`r`nConnection: close`r`n`r`n"
    $headerBytes = [Text.Encoding]::ASCII.GetBytes($header)
    $stream.Write($headerBytes, 0, $headerBytes.Length)
    if ($Body.Length -gt 0) { $stream.Write($Body, 0, $Body.Length) }
    $stream.Flush()
}

try {
    New-Item -ItemType Directory -Path $Runtime -Force | Out-Null
    Remove-Item -LiteralPath $stopFile -Force -ErrorAction SilentlyContinue
    if (-not (Test-Path -LiteralPath $htmlFile)) { throw 'public/index.html is missing' }
    $random = New-Object byte[] 24
    $generator = [Security.Cryptography.RandomNumberGenerator]::Create()
    try { $generator.GetBytes($random) } finally { $generator.Dispose() }
    $token = ([Convert]::ToBase64String($random)).TrimEnd('=').Replace('+','-').Replace('/','_')
    $listener = [Net.Sockets.TcpListener]::new([Net.IPAddress]::Loopback, 0)
    $listener.Start(64)
    $port = ([Net.IPEndPoint]$listener.LocalEndpoint).Port
    $basePath = "/$token/"
    $localUrl = "http://127.0.0.1:$port$basePath"
    $publicUrl = ''
    Write-Status 'local-ready' $localUrl '' ''

    if ($NoTunnel) {
        Write-Status 'local-only' $localUrl '' 'public tunnel disabled'
    } elseif (Install-Cloudflared $localUrl) {
        $cloudflaredReady = $true
        Start-CloudflareTunnel $port $localUrl
    }

    $html = [IO.File]::ReadAllBytes($htmlFile)
    $notFound = [Text.Encoding]::UTF8.GetBytes('{"error":"not found"}')
    while (-not (Test-Path -LiteralPath $stopFile)) {
        if ($cloudflaredReady -and $null -eq $tunnel -and (Get-Date) -ge $tunnelRestartAt) {
            try { Start-CloudflareTunnel $port $localUrl }
            catch {
                Write-Status 'reconnecting-tunnel' $localUrl '' $_.Exception.Message
                $tunnelRestartAt = (Get-Date).AddSeconds($tunnelRestartDelaySeconds)
                $tunnelRestartDelaySeconds = [Math]::Min(15, $tunnelRestartDelaySeconds * 2)
            }
        }

        if ($null -ne $tunnel -and -not $tunnel.HasExited) {
            if ($null -ne $tunnelOutput -and $tunnelOutput.IsCompleted) {
                $line = $tunnelOutput.Result
                if ($null -eq $line) { $tunnelOutput = $null } else {
                    $tunnelLog += $line + "`n"
                    [IO.File]::AppendAllText($tunnelOut, $line + "`r`n")
                    $tunnelOutput = $tunnel.StandardOutput.ReadLineAsync()
                }
            }
            if ($null -ne $tunnelError -and $tunnelError.IsCompleted) {
                $line = $tunnelError.Result
                if ($null -eq $line) { $tunnelError = $null } else {
                    $tunnelLog += $line + "`n"
                    [IO.File]::AppendAllText($tunnelErr, $line + "`r`n")
                    $tunnelError = $tunnel.StandardError.ReadLineAsync()
                }
            }
            if ([string]::IsNullOrEmpty($publicUrl)) {
                $match = [regex]::Match($tunnelLog, 'https://[a-z0-9-]+\.trycloudflare\.com',
                    [Text.RegularExpressions.RegexOptions]::IgnoreCase)
                if ($match.Success -and $tunnelLog.Contains('Registered tunnel connection')) {
                    $publicUrl = $match.Value.TrimEnd('/') + $basePath
                    $tunnelRestartDelaySeconds = 1
                    Write-Status 'public-ready' $localUrl $publicUrl ''
                }
            }
        } elseif ($null -ne $tunnel -and $tunnel.HasExited) {
            $exitCode = $tunnel.ExitCode
            $tunnel.Dispose()
            $tunnel = $null
            $tunnelOutput = $null
            $tunnelError = $null
            $tunnelLog = ''
            $publicUrl = ''
            $tunnelRestartAt = (Get-Date).AddSeconds($tunnelRestartDelaySeconds)
            $tunnelRestartDelaySeconds = [Math]::Min(15, $tunnelRestartDelaySeconds * 2)
            Write-Status 'reconnecting-tunnel' $localUrl '' "Cloudflare Tunnel exited ($exitCode)"
        }

        if (-not $listener.Pending()) { Start-Sleep -Milliseconds 15; continue }
        $client = $listener.AcceptTcpClient()
        try {
            $client.ReceiveTimeout = 1500
            $reader = [IO.StreamReader]::new($client.GetStream(), [Text.Encoding]::ASCII, $false, 2048, $true)
            $request = $reader.ReadLine()
            while (($line = $reader.ReadLine()) -ne $null -and $line.Length -gt 0) { }
            if ([string]::IsNullOrWhiteSpace($request)) { continue }
            $parts = $request.Split(' ')
            $path = if ($parts.Length -ge 2) { [Uri]::UnescapeDataString(($parts[1] -split '\?')[0]) } else { '' }
            if ($parts[0] -ne 'GET') {
                Send-Response $client 405 'Method Not Allowed' 'application/json; charset=utf-8' $notFound
            } elseif ($path -eq $basePath -or $path -eq $basePath.TrimEnd('/')) {
                Send-Response $client 200 'OK' 'text/html; charset=utf-8' $html
            } elseif ($path -eq ($basePath + 'state')) {
                try {
                    $stateInfo = $stateFiles | Where-Object { Test-Path -LiteralPath $_ } |
                        ForEach-Object { Get-Item -LiteralPath $_ } |
                        Sort-Object LastWriteTimeUtc -Descending | Select-Object -First 1
                    if ($null -eq $stateInfo) { throw 'state unavailable' }
                    if (((Get-Date).ToUniversalTime() - $stateInfo.LastWriteTimeUtc).TotalSeconds -gt 2.0) { throw 'stale producer' }
                    $body = [IO.File]::ReadAllBytes($stateInfo.FullName)
                } catch { $body = [Text.Encoding]::UTF8.GetBytes('{"connected":false,"stale":true,"players":[]}') }
                Send-Response $client 200 'OK' 'application/json; charset=utf-8' $body
            } elseif ($path -eq ($basePath + 'map-meta')) {
                try { $body = [IO.File]::ReadAllBytes($mapMetaFile) } catch { $body = [Text.Encoding]::UTF8.GetBytes('null') }
                Send-Response $client 200 'OK' 'application/json; charset=utf-8' $body
			} elseif ($path -eq ($basePath + 'weapon-icons.ttf')) {
				try { $body = [IO.File]::ReadAllBytes($weaponFontFile) } catch { $body = [byte[]]::new(0) }
				if ($body.Length -gt 0) {
					Send-Response $client 200 'OK' 'font/ttf' $body 'private, max-age=86400, immutable'
				} else { Send-Response $client 404 'Not Found' 'application/json; charset=utf-8' $notFound }
            } elseif ($path -eq ($basePath + 'overview')) {
                try { $overviewMap = (Read-SharedText $mapMetaFile | ConvertFrom-Json).map } catch { $overviewMap = '' }
                $overviewMap = ([string]$overviewMap -replace '[^A-Za-z0-9_-]', '_')
                $overviewPrimary = Join-Path $Runtime "overview-$overviewMap-primary.png"
                try { $body = Read-SharedBytes $overviewPrimary } catch { $body = [byte[]]::new(0) }
                if ($body.Length -gt 0) {
                    Send-Response $client 200 'OK' 'image/png' $body 'private, max-age=3600, immutable'
                } else { Send-Response $client 404 'Not Found' 'application/json; charset=utf-8' $notFound }
            } elseif ($path -eq ($basePath + 'overview-lower')) {
                try { $overviewMap = (Read-SharedText $mapMetaFile | ConvertFrom-Json).map } catch { $overviewMap = '' }
                $overviewMap = ([string]$overviewMap -replace '[^A-Za-z0-9_-]', '_')
                $overviewLower = Join-Path $Runtime "overview-$overviewMap-lower.png"
                try { $body = Read-SharedBytes $overviewLower } catch { $body = [byte[]]::new(0) }
                if ($body.Length -gt 0) {
                    Send-Response $client 200 'OK' 'image/png' $body 'private, max-age=3600, immutable'
                } else { Send-Response $client 404 'Not Found' 'application/json; charset=utf-8' $notFound }
            } else {
                Send-Response $client 404 'Not Found' 'application/json; charset=utf-8' $notFound
            }
        } catch { } finally { $client.Dispose() }
    }
} catch {
    $failed = $true
    Write-Status 'error' '' '' $_.Exception.Message
} finally {
    if ($null -ne $tunnel -and -not $tunnel.HasExited) { Stop-Process -Id $tunnel.Id -Force -ErrorAction SilentlyContinue }
    if ($null -ne $listener) { $listener.Stop() }
    if (-not $failed) { Write-Status 'stopped' '' '' '' }
    Remove-Item -LiteralPath $stopFile -Force -ErrorAction SilentlyContinue
}
]====]

__VESTA_WEB_RADAR_HTML = [====[<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1, maximum-scale=1, user-scalable=no, viewport-fit=cover">
<title>Vesta Radar</title>
<link rel="preconnect" href="https://fonts.googleapis.com">
<link rel="preconnect" href="https://fonts.gstatic.com" crossorigin>
<link href="https://fonts.googleapis.com/css2?family=Inter:wght@400;500;600;700;800&display=swap" rel="stylesheet">
<style>
@font-face {
  font-family: VestaWeapons;
  src: url('weapon-icons.ttf') format('truetype');
  font-display: block;
}

:root {
  color-scheme: dark;
  --font: 'Inter', -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, sans-serif;

  --bg-radar: #62656a;
  --bg-panel: rgba(18, 21, 26, 0.92);
  --bg-card: rgba(26, 30, 38, 0.88);
  --bg-card-dead: rgba(26, 30, 38, 0.35);
  --border: rgba(255, 255, 255, 0.08);
  --border-subtle: rgba(255, 255, 255, 0.04);

  --t-color: #efb351;
  --t-glow: rgba(239, 179, 81, 0.15);
  --ct-color: #58aef5;
  --ct-glow: rgba(88, 174, 245, 0.15);

  --green: #4ade80;
  --yellow: #facc15;
  --red: #f87171;

  --text-main: #f8fafc;
  --text-muted: #94a3b8;
  --text-dim: #64748b;

  --sat: env(safe-area-inset-top, 0px);
  --sab: env(safe-area-inset-bottom, 0px);
  --sal: env(safe-area-inset-left, 0px);
  --sar: env(safe-area-inset-right, 0px);
}

* {
  box-sizing: border-box;
  margin: 0;
  padding: 0;
  -webkit-tap-highlight-color: transparent;
  user-select: none;
  font-feature-settings: "tnum" 1, "cv02" 1, "cv03" 1, "cv04" 1;
}

html, body {
  width: 100%;
  height: 100%;
  overflow: hidden;
  background-color: var(--bg-radar);
  font-family: var(--font);
  color: var(--text-main);
  -webkit-font-smoothing: antialiased;
}

/* Base Canvas */
#radar-canvas {
  position: absolute;
  inset: 0;
  width: 100%;
  height: 100%;
  display: block;
}

/* Top Control Bar */
.top-hud {
  position: fixed;
  top: calc(12px + var(--sat));
  left: 50%;
  transform: translateX(-50%);
  display: flex;
  align-items: center;
  gap: 8px;
  z-index: 20;
}

.status-pill {
  height: 32px;
  padding: 0 12px;
  border-radius: 8px;
  background: var(--bg-panel);
  border: 1px solid var(--border);
  backdrop-filter: blur(16px);
  -webkit-backdrop-filter: blur(16px);
  box-shadow: 0 4px 16px rgba(0, 0, 0, 0.25);
  display: inline-flex;
  align-items: center;
  gap: 8px;
  font-size: 11px;
  font-weight: 700;
  letter-spacing: 0.05em;
  text-transform: uppercase;
  color: var(--text-main);
}

.status-dot {
  width: 7px;
  height: 7px;
  border-radius: 50%;
  background: var(--red);
  box-shadow: 0 0 8px var(--red);
  transition: background-color 0.2s ease, box-shadow 0.2s ease;
}
.status-pill.live .status-dot { background: var(--green); box-shadow: 0 0 8px var(--green); }
.status-pill.stale .status-dot { background: var(--yellow); box-shadow: 0 0 8px var(--yellow); }

.settings-btn {
  width: 32px;
  height: 32px;
  border-radius: 8px;
  background: var(--bg-panel);
  border: 1px solid var(--border);
  backdrop-filter: blur(16px);
  -webkit-backdrop-filter: blur(16px);
  display: grid;
  place-items: center;
  color: var(--text-muted);
  cursor: pointer;
  box-shadow: 0 4px 16px rgba(0, 0, 0, 0.25);
  transition: color 0.15s, background-color 0.15s;
}
.settings-btn:hover { color: #fff; background: rgba(30, 35, 45, 0.95); }
.settings-btn svg { width: 16px; height: 16px; fill: none; stroke: currentColor; stroke-width: 2; }

/* Desktop Team Columns */
.desktop-roster {
  position: fixed;
  top: calc(14px + var(--sat));
  bottom: calc(14px + var(--sab));
  width: 240px;
  display: flex;
  flex-direction: column;
  gap: 6px;
  z-index: 10;
  pointer-events: none;
}
.desktop-roster.t { left: calc(14px + var(--sal)); }
.desktop-roster.ct { right: calc(14px + var(--sar)); }

.team-header {
  height: 36px;
  padding: 0 10px;
  border-radius: 8px;
  background: var(--bg-panel);
  border: 1px solid var(--border);
  backdrop-filter: blur(16px);
  -webkit-backdrop-filter: blur(16px);
  display: flex;
  align-items: center;
  justify-content: space-between;
}

.team-meta {
  display: flex;
  align-items: center;
  gap: 6px;
}
.team-tag {
  font-size: 9px;
  font-weight: 800;
  padding: 2px 5px;
  border-radius: 4px;
  letter-spacing: 0.05em;
}
.desktop-roster.t .team-tag { background: var(--t-glow); color: var(--t-color); border: 1px solid rgba(239, 179, 81, 0.3); }
.desktop-roster.ct .team-tag { background: var(--ct-glow); color: var(--ct-color); border: 1px solid rgba(88, 174, 245, 0.3); }

.team-name {
  font-size: 11px;
  font-weight: 700;
  letter-spacing: 0.04em;
  text-transform: uppercase;
}
.desktop-roster.t .team-name { color: var(--t-color); }
.desktop-roster.ct .team-name { color: var(--ct-color); }

.team-total {
  font-size: 11px;
  font-weight: 700;
  color: var(--green);
}

.cards-container {
  display: flex;
  flex-direction: column;
  gap: 5px;
  pointer-events: auto;
  overflow-y: auto;
  scrollbar-width: none;
}
.cards-container::-webkit-scrollbar { display: none; }

/* Player Card Component */
.player-card {
  padding: 7px 9px;
  border-radius: 8px;
  background: var(--bg-card);
  border: 1px solid var(--border);
  backdrop-filter: blur(12px);
  -webkit-backdrop-filter: blur(12px);
  box-shadow: 0 4px 12px rgba(0,0,0,0.15);
  transition: opacity 0.2s ease;
}
.player-card.dead {
  background: var(--bg-card-dead);
  opacity: 0.45;
}

.card-top {
  display: flex;
  align-items: center;
  justify-content: space-between;
  gap: 6px;
  height: 16px;
}

.player-info {
  display: flex;
  align-items: center;
  gap: 6px;
  min-width: 0;
  flex: 1;
}

.player-name {
  font-size: 11.5px;
  font-weight: 600;
  color: #fff;
  overflow: hidden;
  text-overflow: ellipsis;
  white-space: nowrap;
}

.player-stats {
  display: flex;
  align-items: center;
  gap: 8px;
  flex-shrink: 0;
}

.player-money {
  font-size: 10px;
  font-weight: 600;
  color: var(--green);
}

.player-hp {
  font-size: 10.5px;
  font-weight: 700;
  min-width: 26px;
  text-align: right;
}

/* HP Progress Bar */
.hp-track {
  height: 2.5px;
  margin: 5px 0 6px;
  background: rgba(0, 0, 0, 0.45);
  border-radius: 2px;
  overflow: hidden;
}
.hp-fill {
  height: 100%;
  border-radius: 2px;
  transition: width 0.2s ease, background-color 0.2s ease;
}

/* Equipment Row */
.card-bottom {
  display: flex;
  align-items: center;
  height: 16px;
  gap: 4px;
}

.equip {
  position: relative;
  min-width: 22px;
  border: 1px solid transparent;
  display: inline-flex;
  align-items: center;
  justify-content: center;
  height: 16px;
  padding: 0 4px;
  border-radius: 3px;
  color: #cbd5e1;
  background: rgba(255, 255, 255, 0.04);
}
.equip.active {
  color: #fff;
  background: rgba(255, 255, 255, 0.12);
  border: 1px solid rgba(255, 255, 255, 0.15);
}
.equip sup {
  position: absolute;
  right: -2px;
  top: -3px;
  font-size: 7px;
  font-weight: 800;
  color: #fff;
  background: #0f172a;
  padding: 1px 2px;
  border-radius: 3px;
  line-height: 1;
}

.weapon-icon {
  display: inline-flex;
  align-items: center;
  justify-content: center;
  font-family: VestaWeapons, sans-serif;
  font-size: 13.5px;
  line-height: 16px;
  min-width: 14px;
  height: 16px;
}
.equip.active .weapon-icon { font-size: 14.5px; }
.equip.utility .weapon-icon { font-size: 12px; }

.card-meta {
  margin-left: auto;
  display: inline-flex;
  align-items: center;
  gap: 4px;
  font-size: 9.5px;
  font-weight: 700;
}
.armor-item, .kit-item {
  display: inline-flex;
  align-items: center;
  gap: 2px;
}
.armor-item { color: #93c5fd; }
.kit-item { color: #38bdf8; }
.armor-item svg, .kit-item svg {
  width: 12px;
  height: 12px;
  fill: none;
  stroke: currentColor;
  stroke-width: 1.8;
  stroke-linecap: round;
  stroke-linejoin: round;
}

/* Mobile Bottom Dock */
.mobile-roster-dock {
  display: none;
  position: fixed;
  bottom: 0;
  left: 0;
  right: 0;
  padding: 8px 10px calc(8px + var(--sab));
  background: var(--bg-panel);
  border-top: 1px solid var(--border);
  backdrop-filter: blur(20px);
  -webkit-backdrop-filter: blur(20px);
  z-index: 20;
}

.mobile-tabs {
  display: flex;
  gap: 6px;
  margin-bottom: 6px;
}

.tab-btn {
  flex: 1;
  height: 30px;
  border-radius: 6px;
  border: 1px solid var(--border-subtle);
  background: rgba(255, 255, 255, 0.03);
  font-size: 10.5px;
  font-weight: 700;
  letter-spacing: 0.04em;
  text-transform: uppercase;
  display: flex;
  align-items: center;
  justify-content: space-between;
  padding: 0 10px;
  cursor: pointer;
  color: var(--text-muted);
}
.tab-btn.t { color: var(--t-color); }
.tab-btn.ct { color: var(--ct-color); }
.tab-btn.t.active { background: var(--t-glow); border-color: var(--t-color); color: #fff; }
.tab-btn.ct.active { background: var(--ct-glow); border-color: var(--ct-color); color: #fff; }

.mobile-cards-scroll {
  display: flex;
  gap: 6px;
  overflow-x: auto;
  scrollbar-width: none;
  padding: 1px 0;
}
.mobile-cards-scroll::-webkit-scrollbar { display: none; }
.mobile-cards-scroll .player-card {
  min-width: 145px;
  max-width: 165px;
  flex: 0 0 auto;
}

/* Settings Modal */
.settings-menu {
  position: fixed;
  top: calc(52px + var(--sat));
  left: 50%;
  transform: translateX(-50%) scale(0.96);
  width: min(280px, calc(100% - 24px));
  padding: 12px 14px;
  border-radius: 12px;
  background: rgba(18, 21, 26, 0.96);
  border: 1px solid rgba(255, 255, 255, 0.12);
  backdrop-filter: blur(24px);
  -webkit-backdrop-filter: blur(24px);
  box-shadow: 0 20px 40px rgba(0, 0, 0, 0.5);
  opacity: 0;
  pointer-events: none;
  transition: opacity 0.15s ease, transform 0.15s ease;
  z-index: 30;
}
.settings-menu.open {
  opacity: 1;
  pointer-events: auto;
  transform: translateX(-50%) scale(1);
}

.settings-menu h2 {
  font-size: 10px;
  font-weight: 800;
  letter-spacing: 0.08em;
  text-transform: uppercase;
  color: var(--text-dim);
  margin-bottom: 8px;
}

.setting-row {
  height: 32px;
  display: flex;
  align-items: center;
  justify-content: space-between;
  font-size: 12px;
  font-weight: 500;
  color: var(--text-main);
  border-top: 1px solid var(--border-subtle);
}
.setting-row:first-of-type { border-top: none; }

/* Custom Sliders & Toggles */
.toggle-switch {
  appearance: none;
  -webkit-appearance: none;
  width: 34px;
  height: 19px;
  border-radius: 999px;
  background: rgba(255, 255, 255, 0.16);
  position: relative;
  cursor: pointer;
  outline: none;
  transition: background-color 0.2s ease;
}
.toggle-switch::after {
  content: '';
  position: absolute;
  top: 2px;
  left: 2px;
  width: 15px;
  height: 15px;
  border-radius: 50%;
  background: #fff;
  box-shadow: 0 1px 3px rgba(0,0,0,0.3);
  transition: transform 0.2s ease;
}
.toggle-switch:checked { background: #3b82f6; }
.toggle-switch:checked::after { transform: translateX(15px); }

.range-slider {
  appearance: none;
  -webkit-appearance: none;
  width: 90px;
  height: 4px;
  border-radius: 2px;
  background: rgba(255, 255, 255, 0.16);
  outline: none;
}
.range-slider::-webkit-slider-thumb {
  appearance: none;
  -webkit-appearance: none;
  width: 14px;
  height: 14px;
  border-radius: 50%;
  background: #3b82f6;
  border: 2px solid #fff;
  cursor: pointer;
  box-shadow: 0 1px 4px rgba(0,0,0,0.3);
}

@media (max-width: 780px) {
  .desktop-roster { display: none !important; }
  .mobile-roster-dock { display: block; }
  .top-hud { top: calc(8px + var(--sat)); }
}
.card-bottom { height: 18px; gap: 5px; }
.equip,
.equip.active,
.equip.utility {
  height: 18px;
  min-width: 23px;
  padding: 0 5px;
  border: 0;
  border-radius: 4px;
  line-height: 18px;
}
.equip.active { background: rgba(255, 255, 255, 0.12); }
.equip.utility { background: rgba(255, 255, 255, 0.055); }
.weapon-icon,
.equip.active .weapon-icon,
.equip.utility .weapon-icon {
  height: 18px;
  line-height: 18px;
  transform: translateY(-0.5px);
}
</style>
</head>
<body>

<canvas id="radar-canvas"></canvas>

<!-- Top Status & Settings -->
<header class="top-hud">
  <div class="status-pill" id="conn-pill">
    <span class="status-dot"></span>
    <span id="conn-label">Connecting</span>
  </div>
  <button class="settings-btn" id="settings-toggle" aria-label="Settings">
    <svg viewBox="0 0 24 24"><path d="M12.22 2h-.44a2 2 0 0 0-2 2v.18a2 2 0 0 1-1 1.73l-.43.25a2 2 0 0 1-2 0l-.15-.08a2 2 0 0 0-2.73.73l-.22.38a2 2 0 0 0 .73 2.73l.15.1a2 2 0 0 1 1 1.72v.51a2 2 0 0 1-1 1.74l-.15.09a2 2 0 0 0-.73 2.73l.22.38a2 2 0 0 0 2.73.73l.15-.08a2 2 0 0 1 2 0l.43.25a2 2 0 0 1 1 1.73V20a2 2 0 0 0 2 2h.44a2 2 0 0 0 2-2v-.18a2 2 0 0 1 1-1.73l.43-.25a2 2 0 0 1 2 0l.15.08a2 2 0 0 0 2.73-.73l.22-.39a2 2 0 0 0-.73-2.73l-.15-.08a2 2 0 0 1-1-1.74v-.5a2 2 0 0 1 1-1.74l.15-.09a2 2 0 0 0 .73-2.73l-.22-.38a2 2 0 0 0-2.73-.73l-.15.08a2 2 0 0 1-2 0l-.43-.25a2 2 0 0 1-1-1.73V4a2 2 0 0 0-2-2z"/><circle cx="12" cy="12" r="3.2"/></svg>
  </button>
</header>

<!-- Desktop T Panel -->
<aside class="desktop-roster t" id="dt-t-panel">
  <div class="team-header">
    <div class="team-meta">

      <span class="team-name">Terrorists</span>
    </div>
    <span class="team-total" id="dt-t-cash">$0</span>
  </div>
  <div class="cards-container" id="dt-t-cards"></div>
</aside>

<!-- Desktop CT Panel -->
<aside class="desktop-roster ct" id="dt-ct-panel">
  <div class="team-header">
    <div class="team-meta">

      <span class="team-name">Counter-Terrorists</span>
    </div>
    <span class="team-total" id="dt-ct-cash">$0</span>
  </div>
  <div class="cards-container" id="dt-ct-cards"></div>
</aside>

<!-- Mobile Dock -->
<section class="mobile-roster-dock">
  <div class="mobile-tabs">
    <button class="tab-btn t active" id="tab-t" onclick="selectMobileTab(2)">
      <span>Terrorists</span>
      <span id="mob-t-cash">$0</span>
    </button>
    <button class="tab-btn ct" id="tab-ct" onclick="selectMobileTab(3)">
      <span>Counter-Terrorists</span>
      <span id="mob-ct-cash">$0</span>
    </button>
  </div>
  <div class="mobile-cards-scroll" id="mobile-cards-list"></div>
</section>

<!-- Settings Drawer -->
<aside class="settings-menu" id="settings-menu">
  <h2>Radar Settings</h2>
  <label class="setting-row">Terrorists<input class="toggle-switch" type="checkbox" data-setting="terrorists"></label>
  <label class="setting-row">Counter-Terrorists<input class="toggle-switch" type="checkbox" data-setting="counterTerrorists"></label>
  <label class="setting-row">Player Tags & HP<input class="toggle-switch" type="checkbox" data-setting="labels"></label>
  <label class="setting-row">Economy Info<input class="toggle-switch" type="checkbox" data-setting="economy"></label>
  <label class="setting-row">Grenades<input class="toggle-switch" type="checkbox" data-setting="grenades"></label>
  <label class="setting-row">Trajectories<input class="toggle-switch" type="checkbox" data-setting="trajectories"></label>
  <label class="setting-row">Effect Bounds<input class="toggle-switch" type="checkbox" data-setting="zones"></label>
  <label class="setting-row">Bomb Status<input class="toggle-switch" type="checkbox" data-setting="bomb"></label>
  <label class="setting-row">Marker Scale<input class="range-slider" type="range" min="0.7" max="1.5" step="0.05" data-setting="markerSize"></label>
</aside>

<script>
const canvas = document.querySelector('#radar-canvas'),
      ctx = canvas.getContext('2d', { alpha: false }),
      settingsToggle = document.querySelector('#settings-toggle'),
      settingsMenu = document.querySelector('#settings-menu'),
      connPill = document.querySelector('#conn-pill'),
      connLabel = document.querySelector('#conn-label');

const demoMode = location.hostname.endsWith('github.io') ||
                 new URLSearchParams(location.search).get('demo') === '1';

const defaults = {
  terrorists: true,
  counterTerrorists: true,
  labels: true,
  economy: true,
  grenades: true,
  trajectories: true,
  zones: true,
  bomb: true,
  markerSize: 1
};

let stored = {};
try { stored = JSON.parse(localStorage.getItem('vesta-radar-v4') || '{}') || {}; } catch {}
const prefs = { ...defaults, ...stored };

for (const input of document.querySelectorAll('[data-setting]')) {
  const key = input.dataset.setting;
  input[input.type === 'checkbox' ? 'checked' : 'value'] = prefs[key];
  input.addEventListener('input', () => {
    prefs[key] = input.type === 'checkbox' ? input.checked : Number(input.value);
    localStorage.setItem('vesta-radar-v4', JSON.stringify(prefs));
    updateRoster();
  });
}

settingsToggle.onclick = e => { e.stopPropagation(); settingsMenu.classList.toggle('open'); };
document.addEventListener('pointerdown', e => {
  if (!settingsMenu.contains(e.target) && !settingsToggle.contains(e.target)) {
    settingsMenu.classList.remove('open');
  }
});

let currentMobileTab = 2;
function selectMobileTab(team) {
  currentMobileTab = team;
  document.querySelector('#tab-t').classList.toggle('active', team === 2);
  document.querySelector('#tab-ct').classList.toggle('active', team === 3);
  updateRoster();
}

let state = null,
    meta = null,
    mapName = '',
    primaryImage = null,
    lowerImage = null,
    lastOk = 0,
    lastFrame = performance.now(),
    positions = new Map(),
    rosterSequence = -1;

const colors = { 2: '#efb351', 3: '#58aef5' };
const grenadeColors = ['#cbd5e1', '#f87171', '#f8fafc', '#c084fc', '#fb923c', '#fb923c', '#e879f9'];

function fit() {
  const d = Math.min(devicePixelRatio || 1, 2),
        r = canvas.getBoundingClientRect(),
        w = Math.max(1, Math.round(r.width * d)),
        h = Math.max(1, Math.round(r.height * d));
  if (canvas.width !== w || canvas.height !== h) {
    canvas.width = w;
    canvas.height = h;
  }
  ctx.setTransform(d, 0, 0, d, 0, 0);
  return { w: r.width, h: r.height };
}

function mapRect(w, h) {
  const isMobile = w <= 780;
  if (isMobile) {
    const topGap = 52, bottomGap = 115;
    const size = Math.min(w - 16, h - topGap - bottomGap);
    return {
      x: (w - size) / 2,
      y: topGap + (h - topGap - bottomGap - size) / 2,
      w: size,
      h: size
    };
  }

  const sideBar = w <= 1100 ? 245 : 265;
  const size = Math.min(Math.max(200, w - (sideBar * 2)), Math.max(200, h - 28));
  return {
    x: (w - size) / 2,
    y: (h - size) / 2,
    w: size,
    h: size
  };
}

function toMap(v, r) {
  if (!meta || !v) return null;
  return [
    r.x + (v[0] - meta.pos_x) / (meta.scale * meta.width) * r.w,
    r.y + (meta.pos_y - v[1]) / (meta.scale * meta.height) * r.h
  ];
}

/* Precision canvas pill text */
function drawPillTag(str, x, y, size, textColor = '#fff', bgColor = 'rgba(15, 18, 24, 0.85)') {
  ctx.font = `700 ${size}px Inter, -apple-system, sans-serif`;
  ctx.textAlign = 'center';
  ctx.textBaseline = 'middle';
  const padX = 5, padY = 2;
  const metrics = ctx.measureText(str);
  const bw = metrics.width + padX * 2;
  const bh = size + padY * 2 + 1;

  ctx.fillStyle = bgColor;
  ctx.beginPath();
  ctx.roundRect(x - bw / 2, y - bh / 2, bw, bh, 4);
  ctx.fill();
  ctx.strokeStyle = 'rgba(255, 255, 255, 0.1)';
  ctx.lineWidth = 1;
  ctx.stroke();

  ctx.fillStyle = textColor;
  ctx.fillText(str, x, y + 0.5);
}

function drawOutlinedText(str, x, y, size, color = '#fff') {
  ctx.font = `700 ${size}px Inter, -apple-system, sans-serif`;
  ctx.textAlign = 'center';
  ctx.textBaseline = 'middle';
  ctx.lineJoin = 'round';
  ctx.miterLimit = 2;
  ctx.strokeStyle = 'rgba(0, 0, 0, 0.92)';
  ctx.lineWidth = Math.max(2.2, size * 0.30);
  ctx.strokeText(str, x, y);
  ctx.fillStyle = color;
  ctx.fillText(str, x, y);
}

function healthTextColor(value) {
  const hp = Math.max(0, Math.min(100, Number(value) || 0));
  const low = [248, 113, 113], middle = [250, 204, 21], high = [74, 222, 128];
  const from = hp < 50 ? low : middle;
  const to = hp < 50 ? middle : high;
  const amount = hp < 50 ? hp / 50 : (hp - 50) / 50;
  const channel = index => Math.round(from[index] + (to[index] - from[index]) * amount);
  return `rgb(${channel(0)}, ${channel(1)}, ${channel(2)})`;
}

function hull(points) {
  if (points.length < 3) return points;
  points = [...points].sort((a, b) => a[0] - b[0] || a[1] - b[1]);
  const cross = (a, b, c) => (b[0] - a[0]) * (c[1] - a[1]) - (b[1] - a[1]) * (c[0] - a[0]),
        lo = [], hi = [];
  for (const p of points) {
    while (lo.length > 1 && cross(lo.at(-2), lo.at(-1), p) <= 0) lo.pop();
    lo.push(p);
  }
  for (let i = points.length - 1; i >= 0; i--) {
    const p = points[i];
    while (hi.length > 1 && cross(hi.at(-2), hi.at(-1), p) <= 0) hi.pop();
    hi.push(p);
  }
  lo.pop(); hi.pop();
  return lo.concat(hi);
}

/* Perfectly rendered player units */
function drawPlayer(p, r, dt) {
  if ((p.team === 2 && !prefs.terrorists) || (p.team === 3 && !prefs.counterTerrorists)) return;
  const target = toMap(p.origin, r);
  if (!target) return;

  let q = positions.get(p.id) || { x: target[0], y: target[1] };
  const k = 1 - Math.exp(-dt * 30);
  q.x += (target[0] - q.x) * k;
  q.y += (target[1] - q.y) * k;
  positions.set(p.id, q);

  const s = 7 * prefs.markerSize,
        c = colors[p.team] || '#ddd',
        rad = (-(p.angles?.[1] || 0) + 90) * Math.PI / 180;

  // View sector cone
  const fov = 45 * Math.PI / 180;
  const coneLength = s * 3.5;
  ctx.beginPath();
  ctx.moveTo(q.x, q.y);
  ctx.arc(q.x, q.y, coneLength, rad - fov / 2 - Math.PI / 2, rad + fov / 2 - Math.PI / 2);
  ctx.closePath();

  const g = ctx.createRadialGradient(q.x, q.y, s, q.x, q.y, coneLength);
  g.addColorStop(0, c + '66');
  g.addColorStop(1, c + '00');
  ctx.fillStyle = g;
  ctx.fill();

  // Outer dot marker
  ctx.beginPath();
  ctx.arc(q.x, q.y, s, 0, Math.PI * 2);
  ctx.fillStyle = c;
  ctx.fill();

  // White view-direction triangle
  const tipX = q.x + Math.sin(rad) * (s * 1.55);
  const tipY = q.y - Math.cos(rad) * (s * 1.55);
  const baseX = q.x + Math.sin(rad) * (s * 0.85);
  const baseY = q.y - Math.cos(rad) * (s * 0.85);
  const halfWidth = s * 0.68;
  const sideX = Math.cos(rad) * halfWidth;
  const sideY = Math.sin(rad) * halfWidth;
  ctx.beginPath();
  ctx.moveTo(tipX, tipY);
  ctx.lineTo(baseX - sideX, baseY - sideY);
  ctx.lineTo(baseX + sideX, baseY + sideY);
  ctx.closePath();
  ctx.fillStyle = '#ffffff';
  ctx.fill();

  if (prefs.labels) {
    const hp = Math.max(0, Math.min(100, Math.round(Number(p.hp) || 0)));
    const nameY = q.y - s - Math.max(5, 5.5 * prefs.markerSize);
    drawOutlinedText(p.name || 'Player', q.x, nameY,
      Math.max(8, 8.5 * prefs.markerSize), '#ffffff');
    drawOutlinedText(`${hp}%`, q.x, q.y + s + Math.max(7, 8 * prefs.markerSize),
      Math.max(7, 7.5 * prefs.markerSize), healthTextColor(hp));
  }
}

function clean(v) { return String(v || '').replace(/^weapon_/, '').replaceAll('_', ' ').toUpperCase(); }

const weaponGlyphs = {
  knife_ct: ']', knife_t: '[', knife: ']', deagle: 'A', elite: 'B', fiveseven: 'C', glock: 'D',
  revolver: 'J', hkp2000: 'E', p2000: 'E', p250: 'F', usp_silencer: 'G', usps: 'G', tec9: 'H',
  cz75a: 'I', cz75: 'I', mac10: 'K', ump45: 'L', bizon: 'M', mp7: 'N', mp5sd: 'N', mp9: 'R',
  p90: 'O', galilar: 'Q', famas: 'R', m4a1_silencer: 'T', m4a1s: 'T', m4a1: 'S', m4a4: 'S',
  aug: 'U', sg556: 'V', sg553: 'V', ak47: 'W', g3sg1: 'X', scar20: 'Y', awp: 'Z', ssg08: 'a',
  xm1014: 'b', sawedoff: 'c', sawed_off: 'c', mag7: 'd', nova: 'e', negev: 'f', m249: 'g',
  taser: 'h', flashbang: 'i', hegrenade: 'j', smokegrenade: 'k', molotov: 'l', decoy: 'm', incgrenade: 'n', c4: 'o'
};

function weaponGlyph(raw) {
  const n = String(raw || '').replace(/^weapon_/, '').toLowerCase();
  return weaponGlyphs[n] || '?';
}

function weaponIcon(raw) {
  return `<span class="weapon-icon" title="${escapeHtml(clean(raw) || 'Equipment')}">${escapeHtml(weaponGlyph(raw))}</span>`;
}

const armorSvg = '<svg viewBox="0 0 16 16"><path d="M3 3l5-2 5 2v4c0 3.3-1.8 5.9-5 7.5C4.8 12.9 3 10.3 3 7z"/></svg>',
      helmetSvg = '<svg viewBox="0 0 16 16"><path d="M2.5 9a5.5 5.5 0 0111 0v2H9l-1.5 2H3zM11 9v2"/></svg>',
      kitSvg = '<svg viewBox="0 0 16 16"><path d="M4 2l3.4 4.1M12 2L8.6 6.1M7.4 6.1L3 14M8.6 6.1L13 14M5 9h6"/></svg>';

function circleWorld(origin, radius, r) {
  const a = toMap(origin, r),
        b = toMap([origin[0] + radius, origin[1], origin[2] || 0], r);
  return a && b ? { x: a[0], y: a[1], radius: Math.hypot(b[0] - a[0], b[1] - a[1]) } : null;
}

function drawProjectile(p, r) {
  const c = grenadeColors[p.kind] || '#eee';
  if (prefs.trajectories && p.trajectory?.length > 1) {
    ctx.beginPath();
    let first = true;
    for (const point of p.trajectory) {
      const q = toMap(point, r);
      if (!q) continue;
      first ? (ctx.moveTo(...q), first = false) : ctx.lineTo(...q);
    }
    ctx.strokeStyle = c + 'b0';
    ctx.lineWidth = 1.4;
    ctx.lineCap = 'round';
    ctx.stroke();
  }

  if (prefs.zones && (p.kind === 4 || p.kind === 5) && p.fire_points?.length > 2) {
    const points = [];
    for (const fire of p.fire_points) {
      for (let i = 0; i < 8; i++) {
        const a = i * Math.PI / 4,
              edge = [fire[0] + Math.cos(a) * 60, fire[1] + Math.sin(a) * 60, fire[2]],
              q = toMap(edge, r);
        if (q) points.push(q);
      }
    }
    const poly = hull(points);
    if (poly.length > 2) {
      ctx.beginPath();
      poly.forEach((q, i) => i ? ctx.lineTo(...q) : ctx.moveTo(...q));
      ctx.closePath();
      ctx.fillStyle = 'rgba(251, 146, 60, 0.22)';
      ctx.fill();
      ctx.strokeStyle = 'rgba(251, 146, 60, 0.85)';
      ctx.lineWidth = 1.4;
      ctx.stroke();
    }
  } else if (prefs.zones && p.smoke) {
    const z = circleWorld(p.origin, 144, r);
    if (z) {
      ctx.beginPath();
      ctx.arc(z.x, z.y, z.radius, 0, Math.PI * 2);
      ctx.fillStyle = 'rgba(241, 245, 249, 0.2)';
      ctx.fill();
      ctx.strokeStyle = 'rgba(241, 245, 249, 0.7)';
      ctx.lineWidth = 1.3;
      ctx.stroke();
    }
  }

  if (!prefs.grenades) return;
  const q = toMap(p.origin, r);
  if (!q) return;

  ctx.beginPath();
  ctx.arc(q[0], q[1], 8.5, 0, Math.PI * 2);
  ctx.fillStyle = '#0f1218';
  ctx.fill();
  ctx.strokeStyle = c;
  ctx.lineWidth = 1.6;
  ctx.stroke();

  const names = ['', 'hegrenade', 'flashbang', 'smokegrenade', 'molotov', 'molotov', 'decoy'],
        time = p.effect_remaining > 0 ? p.effect_remaining : p.remaining > 0 ? p.remaining : 0;
  ctx.font = '400 13px VestaWeapons';
  ctx.textAlign = 'center';
  ctx.textBaseline = 'middle';
  ctx.fillStyle = c;
  ctx.fillText(weaponGlyph(names[p.kind] || ''), q[0], q[1] + 1);

  if (time > 0) drawPillTag(`${time.toFixed(1)}s`, q[0], q[1] + 15, 7.5, c);
}

function drawBomb(r) {
  if (!prefs.bomb || !state?.bomb?.active) return;
  const b = state.bomb,
        q = toMap(b.origin, r);

  const bombColor = b.defusing ? '#58aef5' : '#efb351';

  if (q) {
    ctx.beginPath();
    ctx.arc(q[0], q[1], 10, 0, Math.PI * 2);
    ctx.fillStyle = '#0f1218';
    ctx.fill();
    ctx.strokeStyle = bombColor;
    ctx.lineWidth = 1.8;
    ctx.stroke();

    ctx.font = '400 15px VestaWeapons';
    ctx.textAlign = 'center';
    ctx.textBaseline = 'middle';
    ctx.fillStyle = bombColor;
    ctx.fillText('o', q[0], q[1] + 1);

    if (b.planted) drawPillTag(`SITE ${b.site === 1 ? 'B' : 'A'}`, q[0], q[1] + 16, 8, '#f8fafc');
  }

  if (!b.planted) return;
  const x = r.x + r.w / 2,
        y = r.y + 36,
        def = b.defusing ? `  DEF ${Number(b.defuse || 0).toFixed(1)}s` : '',
        label = `SITE ${b.site === 1 ? 'B' : 'A'}  •  ${Number(b.time || 0).toFixed(1)}s${def}`;

  drawPillTag(label, x, y, 11, b.defusing ? '#58aef5' : b.time < 10 ? '#f87171' : '#efb351', 'rgba(15, 18, 24, 0.92)');
}

function localEntry() {
  const p = state?.local;
  if (!p?.alive || !p.origin) return null;
  return {
    id: -1,
    name: p.name || 'Player',
    team: p.team,
    hp: p.hp,
    armor: 0,
    money: 0,
    origin: p.origin,
    angles: p.angles,
    weapon: '',
    loadout: [],
    local: true
  };
}

function render(now) {
  const { w, h } = fit(),
        dt = Math.min(0.1, (now - lastFrame) / 1000);
  lastFrame = now;

  ctx.fillStyle = '#62656a';
  ctx.fillRect(0, 0, w, h);

  const r = mapRect(w, h),
        image = state?.layer === 'lower' && lowerImage ? lowerImage : primaryImage;

  if (image?.complete && image.naturalWidth) {
    ctx.imageSmoothingEnabled = true;
    ctx.imageSmoothingQuality = 'high';
    ctx.drawImage(image, r.x, r.y, r.w, r.h);
  } else {
    ctx.fillStyle = '#575a5f';
    ctx.fillRect(r.x, r.y, r.w, r.h);
  }

  if (state && meta) {
    for (const p of state.projectiles || []) drawProjectile(p, r);
    for (const p of state.players || []) drawPlayer(p, r, dt);
    const local = localEntry();
    if (local) drawPlayer(local, r, dt);
    drawBomb(r);
  }

  requestAnimationFrame(render);
}

function equipHtml(p) {
  const counts = {};
  for (const raw of p.loadout || []) {
    const key = String(raw || '').toLowerCase();
    counts[key] = (counts[key] || 0) + 1;
  }
  const active = String(p.weapon || ''),
        utilities = ['flashbang', 'hegrenade', 'smokegrenade', 'molotov', 'incgrenade', 'decoy'];

  let output = active ? `<span class="equip active">${weaponIcon(active)}</span>` : '';

  for (const [raw, count] of Object.entries(counts)) {
    const kind = raw.replace(/^weapon_/, '');
    if (!utilities.includes(kind)) continue;
    output += `<span class="equip utility">${weaponIcon(raw)}${count > 1 ? `<sup>${count}</sup>` : ''}</span>`;
  }

  output += '<div class="card-meta">';
  if (p.armor) output += `<span class="armor-item">${p.helmet ? helmetSvg : armorSvg}<span>${Math.max(0, p.armor)}</span></span>`;
  if (p.kit) output += `<span class="kit-item" title="Defuse kit">${kitSvg}</span>`;
  output += '</div>';

  return output;
}

function escapeHtml(v) {
  return String(v || '').replace(/[&<>"']/g, c => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;' }[c]));
}

function createCard(p) {
  const hp = Math.max(0, Math.min(100, p.hp)),
        isDead = hp <= 0,
        healthColor = hp > 50 ? '#4ade80' : hp > 20 ? '#facc15' : '#f87171';

  return `
    <article class="player-card ${isDead ? 'dead' : ''}">
      <div class="card-top">
        <div class="player-info">
          <span class="player-name">${escapeHtml(p.name || 'Player')}</span>
        </div>
        <div class="player-stats">
          <span class="player-money">$${Number(p.money || 0).toLocaleString()}</span>
          <span class="player-hp" style="color:${healthColor}">${hp}</span>
        </div>
      </div>
      <div class="hp-track">
        <div class="hp-fill" style="width:${hp}%;background:${healthColor}"></div>
      </div>
      <div class="card-bottom">${equipHtml(p)}</div>
    </article>
  `;
}

function updateRoster() {
  if (!state) return;
  const players = [...(state.players || [])],
        local = localEntry();
  if (local) players.push(local);

  const t = players.filter(p => p.team === 2),
        ct = players.filter(p => p.team === 3);

  const tCash = '$' + t.reduce((n, p) => n + Number(p.money || 0), 0).toLocaleString();
  const ctCash = '$' + ct.reduce((n, p) => n + Number(p.money || 0), 0).toLocaleString();

  // Desktop Totals
  document.querySelector('#dt-t-cash').textContent = tCash;
  document.querySelector('#dt-ct-cash').textContent = ctCash;

  // Mobile Totals
  document.querySelector('#mob-t-cash').textContent = tCash;
  document.querySelector('#mob-ct-cash').textContent = ctCash;

  // Render Desktop lists
  document.querySelector('#dt-t-cards').innerHTML = prefs.economy ? t.map(createCard).join('') : '';
  document.querySelector('#dt-ct-cards').innerHTML = prefs.economy ? ct.map(createCard).join('') : '';

  document.querySelector('#dt-t-panel').style.display = prefs.terrorists ? 'flex' : 'none';
  document.querySelector('#dt-ct-panel').style.display = prefs.counterTerrorists ? 'flex' : 'none';

  // Render Mobile stream
  const mobList = currentMobileTab === 2 ? t : ct;
  document.querySelector('#mobile-cards-list').innerHTML = prefs.economy ? mobList.map(createCard).join('') : '';
}

async function loadMap(name) {
  try {
    const response = await fetch(demoMode ? 'demo-meta.json' : 'map-meta', { cache: 'no-store' });
    if (!response.ok) throw Error(response.status);
    const nextMeta = await response.json();
    if (!nextMeta) return;
    meta = nextMeta;

    const suffix = '?map=' + encodeURIComponent(name) + '&v=5';
    primaryImage = new Image();
    primaryImage.src = demoMode ? 'assets/overview-de_mirage.png' : 'overview' + suffix;
    lowerImage = null;
    if (meta.has_lower) {
      lowerImage = new Image();
      lowerImage.src = demoMode ? 'assets/overview-de_mirage.png' : 'overview-lower' + suffix;
    }
  } catch {}
}

async function poll() {
  const controller = new AbortController(),
        timer = setTimeout(() => controller.abort(), 1800);
  try {
    const response = await fetch(demoMode ? 'demo-state.json' : 'state', { cache: 'no-store', signal: controller.signal });
    if (!response.ok) throw Error(response.status);
    const next = await response.json();

    if (next.connected) {
      state = next;
      lastOk = Date.now();
      connPill.className = 'status-pill live';
      connLabel.textContent = next.map || 'Live';
      if (next.map && next.map !== mapName) {
        mapName = next.map;
        loadMap(mapName);
      }
      if (next.sequence !== rosterSequence) {
        rosterSequence = next.sequence;
        updateRoster();
      }
    } else {
      connPill.className = state ? 'status-pill stale' : 'status-pill';
      connLabel.textContent = state ? 'Reconnecting' : 'Waiting';
    }
  } catch {
    if (Date.now() - lastOk > 900) {
      connPill.className = state ? 'status-pill stale' : 'status-pill';
      connLabel.textContent = state ? 'Reconnecting' : 'Offline';
    }
  } finally {
    clearTimeout(timer);
    setTimeout(poll, demoMode ? 1000 : 50);
  }
}

requestAnimationFrame(render);
poll();
</script>
</body>
</html>
]====]

-- Vesta Web Radar. vesta.exe remains offline: network access exists only in
-- this explicitly enabled Lua package and its helper process.

local root = vesta.api.script_dir
local runtime = vesta.api.data_dir
local separator = package.config:sub(1, 1)
local embedded_helper = rawget(_G, "__VESTA_WEB_RADAR_HELPER")
local embedded_html = rawget(_G, "__VESTA_WEB_RADAR_HTML")
local helper_relative = embedded_helper and ".vesta-web-radar-helper.ps1" or "helper.ps1"
local html_path = embedded_html and (runtime .. separator .. "web-radar.html")
    or (root .. separator .. "public" .. separator .. "index.html")
local identity = tostring({}):match("0x(%x+)") or tostring(os.time())
local session = tostring(os.time()) .. "-" .. identity
local prefix = runtime .. separator .. session .. "-"
local state_paths = {prefix .. "state-a.json", prefix .. "state-b.json"}
local state_temporary = prefix .. "state.tmp"
local state_slot = 1
local map_meta_path = prefix .. "map-meta.json"
local map_meta_tmp = prefix .. "map-meta.tmp"
local status_path = prefix .. "status.json"
local stop_path = prefix .. "stop.flag"

local running = false
local status = "Starting..."
local share_url = ""
local next_write_us = 0
local write_accumulator = 0
local status_accumulator = 0
local map_name = ""
local overview = nil
local trajectory_cache = {}
local effect_cache = {}
local copy_feedback_until = 0
local copy_action = "Starting..."

local function write_file(path, value)
    local file = io.open(path, "wb")
    if not file then return false end
    file:write(value)
    file:close()
    return true
end

local function ensure_embedded_assets()
    if not embedded_helper then return true end
    if type(embedded_helper) ~= "string" or type(embedded_html) ~= "string" then return false end
    return write_file(root .. separator .. helper_relative, embedded_helper)
        and write_file(html_path, embedded_html)
end

local function ensure_weapon_font()
	local target = prefix .. "weapon-icons.ttf"
    local existing = io.open(target, "rb")
    if existing then
        local size = existing:seek("end") or 0
        existing:close()
        if size > 1024 then return true end
    end
    local bytes = vesta.assets.weapon_font()
    if type(bytes) ~= "string" or #bytes < 1024 then return false end
    local file = io.open(target, "wb")
    if not file then return false end
    file:write(bytes)
    file:close()
    return true
end

vesta.ui.button("copy", "Copy link", "Starting...")

local function set_copy_action(value)
	value = tostring(value or "")
	if value == copy_action then return end
	copy_action = value
	vesta.ui.button("copy", "Copy link", value)
end

local function json_string(value)
    local escaped = tostring(value or "")
        :gsub('\\', '\\\\'):gsub('"', '\\"')
        :gsub('\b', '\\b'):gsub('\f', '\\f')
        :gsub('\n', '\\n'):gsub('\r', '\\r'):gsub('\t', '\\t')
    return '"' .. escaped .. '"'
end

local function number(value, fallback)
    value = tonumber(value)
    if not value or value ~= value or value == math.huge or value == -math.huge then
        return fallback or 0
    end
    return value
end

local function bool(value) return value and "true" or "false" end
local function normalize_map(value)
    value = tostring(value or ""):gsub('\\', '/')
    return value:match('([^/]+)%.vpk$') or value:match('([^/]+)$') or ""
end
local function vec3(value)
    value = value or {}
    return string.format('[%.2f,%.2f,%.2f]', number(value.x), number(value.y), number(value.z))
end

local function read_file(path)
    local file = io.open(path, "rb")
    if not file then return nil end
    local value = file:read("*a")
    file:close()
    return value
end

local function write_atomic_path(path, temporary, value)
    local file = io.open(temporary, "wb")
    if not file then return false end
    file:write(value)
    file:close()
    os.remove(path)
    return os.rename(temporary, path) ~= nil
end

local function write_state(value)
    local target = state_paths[state_slot]
    if not write_atomic_path(target, state_temporary, value) then return false end
    state_slot = state_slot == 1 and 2 or 1
    return true
end

local function write_stop()
    local file = io.open(stop_path, "wb")
    if file then file:write("stop"); file:close() end
end

local function json_field(source, name)
    if not source then return nil end
    local raw = source:match('"' .. name .. '"%s*:%s*"(.-)"')
    if not raw then return nil end
    return raw:gsub('\\/', '/'):gsub('\\"', '"'):gsub('\\\\', '\\')
end

local function refresh_status()
    local source = read_file(status_path)
    if not source then return end
    local phase = json_field(source, "status")
    local public = json_field(source, "public_url")
    local err = json_field(source, "error")
    running = phase ~= "stopped" and phase ~= "error"
    if public and public ~= "" then
        share_url, status = public, "Ready - private link available"
        if os.clock() >= copy_feedback_until then set_copy_action("Copy link") end
    elseif phase == "downloading-cloudflared" then
        status = "Downloading Cloudflare Tunnel..."
    elseif phase == "verifying-cloudflared" then
        status = "Verifying Cloudflare Tunnel..."
    elseif phase == "reconnecting-tunnel" then
        status = "Reconnecting Cloudflare Tunnel..."
    elseif phase == "local-only" then
        status = err and err ~= "" and ("Tunnel unavailable: " .. err) or "Public tunnel unavailable"
    elseif phase == "error" then
        status = err and err ~= "" and ("Error: " .. err) or "Server error"
        set_copy_action("Unavailable")
    else
        status = "Opening private link..."
        set_copy_action("Starting...")
    end
end

local function prepare_overview(frame)
    map_name = normalize_map(frame.map)
    trajectory_cache = {}
    effect_cache = {}
    overview = nil
    if map_name == "" then return end
    local value, err = vesta.game.radar_overview(map_name, "overview-" .. map_name)
    if not value then
        status = "Map unavailable: " .. tostring(err or map_name)
        set_copy_action("Map unavailable")
        return
    end
    overview = value
    local meta = string.format(
        '{"map":%s,"pos_x":%.3f,"pos_y":%.3f,"scale":%.5f,"width":%d,"height":%d,' ..
        '"has_lower":%s,"lower_altitude_max":%.3f}',
        json_string(map_name), number(value.pos_x), number(value.pos_y), number(value.scale, 1),
        number(value.width, 1024), number(value.height, 1024), bool(value.has_lower),
        number(value.lower_altitude_max))
    write_atomic_path(map_meta_path, map_meta_tmp, meta)
end

local grenade_names = {[1]="he",[2]="flash",[3]="smoke",[4]="molotov",[6]="decoy"}
local function trajectory_json(frame, projectile)
    local kind = grenade_names[number(projectile.kind)]
	local velocity = projectile.initial_velocity or {}
	local speed_sq = number(velocity.x)^2 + number(velocity.y)^2 + number(velocity.z)^2
	if speed_sq <= 64 then
		velocity = projectile.velocity or {}
		speed_sq = number(velocity.x)^2 + number(velocity.y)^2 + number(velocity.z)^2
	end
	local origin = projectile.initial_position or {}
	if number(origin.x)^2 + number(origin.y)^2 + number(origin.z)^2 <= 1 then
		origin = projectile.origin or {}
	end
	local moving = speed_sq > 64
    if not kind or not moving or projectile.detonated or projectile.smoke_active then return "[]", false end
    local id = number(projectile.id)
    local cached = trajectory_cache[id]
	local signature = string.format("%.2f:%.2f:%.2f:%.2f:%.2f:%.2f:%s",
		number(origin.x), number(origin.y), number(origin.z), number(velocity.x),
		number(velocity.y), number(velocity.z), kind)
	if not cached or cached.signature ~= signature then
		local predicted = vesta.game.predict_grenade(origin, velocity, kind, -1)
        local points = {}
        if predicted and predicted.valid then
            local source = predicted.points or {}
            local stride = math.max(1, math.ceil(#source / 48))
            for index = 1, #source, stride do points[#points+1] = vec3(source[index]) end
            if #source > 0 and ((#source - 1) % stride) ~= 0 then points[#points+1] = vec3(source[#source]) end
        end
		cached = {signature=signature, json="[" .. table.concat(points, ",") .. "]"}
        trajectory_cache[id] = cached
		return cached.json, true
    end
    return cached.json, false
end

local function prime_one_trajectory(frame)
    for _, projectile in ipairs(frame.projectiles or {}) do
        local _, generated = trajectory_json(frame, projectile)
        if generated then return true end
    end
    return false
end

local function bomb_position(frame, bomb)
    if bomb.planted and bomb.position then return bomb.position end
	if bomb.active and bomb.active_position then return bomb.active_position end
    for _, item in ipairs(frame.items or {}) do
        if number(item.kind) == 37 then return item.origin end
    end
    for _, player in ipairs(frame.players or {}) do
        for _, name in ipairs(player.loadout or {}) do
            name = tostring(name):lower():gsub("^weapon_", "")
            if name == "c4" then return player.origin end
        end
    end
    return nil
end

local function string_array(values)
    local output = {}
    for _, value in ipairs(values or {}) do output[#output+1] = json_string(value) end
    return "[" .. table.concat(output, ",") .. "]"
end

local function encode_players(frame)
    local output = {}
    for _, p in ipairs(frame.players or {}) do
        local weapon = p.weapon or {}
        output[#output+1] = string.format(
            '{"id":%d,"name":%s,"team":%d,"hp":%d,"armor":%d,"origin":%s,"velocity":%s,"angles":%s,' ..
            '"weapon":%s,"ammo":%d,"max_ammo":%d,"money":%d,"helmet":%s,"kit":%s,"scoped":%s,' ..
            '"loadout":%s,"visible":%s,"spotted":%s,"immune":%s}',
            number(p.handle), json_string(p.name), number(p.team), number(p.health), number(p.armor),
            vec3(p.origin), vec3(p.velocity), vec3(p.eye_angles), json_string(weapon.name), number(weapon.ammo),
            number(weapon.max_ammo), number(p.money), bool(p.helmet), bool(p.defuser), bool(p.scoped),
            string_array(p.loadout), bool(p.visible), bool(p.spotted), bool(p.invulnerable))
    end
    return '[' .. table.concat(output, ',') .. ']'
end

local function encode_projectiles(frame)
    local output, alive = {}, {}
    for _, p in ipairs(frame.projectiles or {}) do
        local id = number(p.id)
        alive[id] = true
        local kind = number(p.kind)
        local active_effect = p.smoke_active or kind == 5
            or (kind == 6 and number(p.effect_tick_begin) > 0)
        local duration = kind == 3 and 20 or kind == 5 and 7 or kind == 6 and 15 or 0
        local effect = effect_cache[id]
        if active_effect and not effect then
            effect = {started=number(frame.timestamp_us)}
            effect_cache[id] = effect
        end
        local precise_expire = number(p.expire_time)
        local game_time = number(frame.local_player and frame.local_player.game_time)
        local effect_remaining = precise_expire > game_time and game_time > 0
            and (precise_expire - game_time)
            or (effect and math.max(0,
                duration - (number(frame.timestamp_us) - effect.started) / 1000000) or -1)
        -- HE/flash entities can survive briefly after their one-shot effect. Do not
        -- leave a stale browser marker during that network cleanup interval.
        if not ((kind == 1 or kind == 2) and p.detonated)
            and not (active_effect and duration > 0 and effect_remaining <= 0) then
            local fires = {}
            for _, point in ipairs(p.fire_points or {}) do fires[#fires+1] = vec3(point) end
            local trajectory = trajectory_json(frame, p)
            output[#output+1] = string.format(
                '{"id":%d,"kind":%d,"origin":%s,"velocity":%s,"remaining":%.2f,' ..
                '"effect_remaining":%.2f,"detonated":%s,"smoke":%s,"bounces":%d,' ..
                '"spawn":%.3f,"detonate":%.3f,"expire":%.3f,"fire_points":[%s],"trajectory":%s}',
                id, kind, vec3(p.origin), vec3(p.velocity), number(p.remaining_lifetime),
                effect_remaining, bool(p.detonated), bool(p.smoke_active), number(p.bounces),
                number(p.spawn_time), number(p.detonate_time), number(p.expire_time),
                table.concat(fires, ","), trajectory)
        end
    end
    for id in pairs(trajectory_cache) do if not alive[id] then trajectory_cache[id] = nil end end
    for id in pairs(effect_cache) do if not alive[id] then effect_cache[id] = nil end end
    return '[' .. table.concat(output, ',') .. ']'
end

local function encode_frame(frame)
    local camera = frame.camera or {}
    local local_player = frame.local_player or {}
	local bomb = frame.bomb or {}
	local bomb_origin = bomb_position(frame, bomb)
    local lower = overview and overview.has_lower and camera.origin
        and number(camera.origin.z) <= number(overview.lower_altitude_max)
    return string.format(
        '{"version":2,"sequence":%d,"timestamp_us":%d,"connected":%s,"map":%s,"layer":%s,' ..
        '"camera":{"origin":%s,"angles":%s,"fov":%.2f},"game_time":%.3f,' ..
		'"local":{"name":%s,"team":%d,"alive":%s,"hp":%d,"origin":%s,"angles":%s},"players":%s,"projectiles":%s,' ..
		'"bomb":{"active":%s,"planted":%s,"origin":%s,"time":%.2f,"defusing":%s,"defuse":%.2f,"site":%d,"damage":%d}}',
        number(frame.sequence), number(frame.timestamp_us), bool(frame.connected), json_string(map_name),
        json_string(lower and "lower" or "primary"), vec3(camera.origin), vec3(camera.angles),
		number(camera.fov), number(local_player.game_time), json_string(local_player.name), number(local_player.team), bool(local_player.alive), number(local_player.health),
		vec3(camera.origin), vec3(camera.angles), encode_players(frame), encode_projectiles(frame), bool(bomb_origin ~= nil), bool(bomb.planted), vec3(bomb_origin), number(bomb.time_remaining),
        bool(bomb.being_defused), number(bomb.defuse_remaining), number(bomb.site), number(bomb.predicted_damage))
end

local function start_server()
    if running then return end
	if not ensure_embedded_assets() then
		status = "Embedded web assets unavailable"
		set_copy_action("Unavailable")
		return
	end
	if not ensure_weapon_font() then
		status = "Weapon font unavailable"
		set_copy_action("Unavailable")
		return
	end
    write_stop(); os.remove(status_path); os.remove(stop_path)
    local started, launch_error = vesta.helpers.start(helper_relative,
        {"-Root", root, "-Runtime", runtime, "-Session", session,
		 "-HtmlFile", html_path})
    if not started then
        running, status = false, "Error: " .. tostring(launch_error or "helper launch failed")
        set_copy_action("Unavailable")
        return
    end
    running, status = true, "Starting local server"
    set_copy_action("Starting...")
end

vesta.events.on("tick", function(delta)
    delta = math.max(0, math.min(number(delta, 0.016), 0.1))
    write_accumulator = write_accumulator + delta
    status_accumulator = status_accumulator + delta
    if os.clock() >= copy_feedback_until
        and vesta.ui.consume("copy") and share_url:match('^https?://') then
        if vesta.helpers.copy_text(share_url) then
            copy_feedback_until = os.clock() + 1.2
            set_copy_action("Copied")
        end
    end
    if status_accumulator >= 0.25 then
        status_accumulator = status_accumulator % 0.25
        refresh_status()
    end
    if running and write_accumulator >= 0.05 then
        write_accumulator = write_accumulator % 0.05
        local frame = vesta.game.radar_snapshot()
        if frame then
            if normalize_map(frame.map) ~= map_name then prepare_overview(frame) end
            if number(frame.timestamp_us) >= next_write_us and not prime_one_trajectory(frame) then
                write_state(encode_frame(frame))
                next_write_us = number(frame.timestamp_us) + 50000 -- 20 Hz producer
            end
        end
    end
end)

vesta.events.on("load", start_server)
vesta.events.on("unload", write_stop)
