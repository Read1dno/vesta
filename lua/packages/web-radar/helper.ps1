param(
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
