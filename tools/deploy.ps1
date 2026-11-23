# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Rahmat Hidayat
# Deploy satu klik: bangun web + firmware, flash ESP32 lewat WiFi (OTA), lalu deploy Cloudflare Worker.
# Pakai: klik ganda tools\deploy.cmd, atau  powershell -File tools\deploy.ps1 [-SkipEsp] [-SkipWorker]
# Konfigurasi dibaca dari .env.local di akar repo (git-ignored). Contoh: .env.example
param([switch]$SkipEsp, [switch]$SkipWorker)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
Set-Location $root
$env:PYTHONIOENCODING = 'utf-8'
$env:PATH = "$env:USERPROFILE\.platformio\penv\Scripts;$env:PATH"

function Step($m) { Write-Host "`n== $m" -ForegroundColor Cyan }
function Die($m) { Write-Host "GAGAL: $m" -ForegroundColor Red; exit 1 }
function Run($label, [scriptblock]$b) { & $b; if ($LASTEXITCODE -ne 0) { Die "$label (kode $LASTEXITCODE)" } }

# ---- .env.local ----
$envFile = Join-Path $root '.env.local'
if (-not (Test-Path $envFile)) { Die ".env.local belum ada. Salin .env.example menjadi .env.local lalu isi DEPLOY_ESP_PASSWORD." }
$cfg = @{}
foreach ($l in Get-Content $envFile -Encoding UTF8) {
  if ($l -match '^\s*([A-Z0-9_]+)\s*=\s*(.*?)\s*$' -and $l -notmatch '^\s*#') { $cfg[$Matches[1]] = $Matches[2].Trim('"') }
}
$hostName = if ($cfg['DEPLOY_ESP_HOST']) { $cfg['DEPLOY_ESP_HOST'] } else { 'mesincuci.local' }
$pass = $cfg['DEPLOY_ESP_PASSWORD']
if (-not $SkipEsp -and -not $pass) { Die 'DEPLOY_ESP_PASSWORD kosong di .env.local' }
$base = "http://$hostName"
$jar = Join-Path $env:TEMP 'mesin-cuci-deploy.cookies'
$tmpBody = Join-Path $env:TEMP 'mesin-cuci-deploy.json'

function Api($method, $path, $body) {
  # Kata sandi dikirim lewat berkas sementara (tidak tampil di daftar proses), dihapus setelah dipakai.
  $a = @('-s', '-m', '20', '-b', $jar, '-c', $jar, '-X', $method, '-w', "`n%{http_code}", "$base$path")
  if ($body) {
    [IO.File]::WriteAllText($tmpBody, $body, (New-Object Text.UTF8Encoding $false))
    $a = @('-H', 'Content-Type: application/json', '--data-binary', "@$tmpBody") + $a
  }
  $out = & curl.exe @a
  if (Test-Path $tmpBody) { Remove-Item $tmpBody -Force }
  $txt = ($out -join "`n"); $i = $txt.LastIndexOf("`n")
  if ($i -lt 0) { return @{ code = 0; body = '' } }
  @{ code = [int]$txt.Substring($i + 1); body = $txt.Substring(0, $i) }
}
function Login {
  $r = Api 'POST' '/api/login' (@{ password = $pass; remember = $false } | ConvertTo-Json -Compress)
  if ($r.code -eq 429) { Die "Login dikunci sementara: $($r.body)" }
  if ($r.code -ne 200) { Die "Login ESP32 gagal (HTTP $($r.code)). Cek DEPLOY_ESP_PASSWORD." }
}

# ---- 1. Bangun ----
Step 'Bangun web (web_ui.h + cloud/public/index.html)'
Run 'build web' { python tools/build_web.py --remote }
if (-not $SkipEsp) {
  Step 'Bangun firmware'
  Run 'pio run' { pio run -e rymcu-esp32-devkitc }
}

# ---- 2. Flash ESP32 lewat WiFi ----
if (-not $SkipEsp) {
  Step "Cek ESP32 di $hostName"
  $ping = Api 'GET' '/api/info' $null
  if ($ping.code -eq 0) { Die "ESP32 tidak terjangkau di $hostName. Satu jaringan WiFi? Atau isi DEPLOY_ESP_HOST dengan IP." }
  Login
  $job = Api 'GET' '/api/job' $null
  if ($job.code -ne 200 -or $job.body -notmatch '"active"\s*:\s*false') { Die "Mesin sedang bekerja atau sibuk (HTTP $($job.code)). Hentikan Proses dulu." }

  $bin = Join-Path $root '.pio\build\rymcu-esp32-devkitc\firmware.bin'
  if (-not (Test-Path $bin)) { Die "firmware.bin tidak ditemukan: $bin" }
  Step ("Unggah firmware ({0:N0} byte)" -f (Get-Item $bin).Length)
  $out = & curl.exe -s -m 240 -H 'Expect:' -b $jar -c $jar -F "file=@$bin;filename=firmware.bin" -w "`n%{http_code}" "$base/api/update"
  $txt = ($out -join "`n"); $i = $txt.LastIndexOf("`n")
  $code = if ($i -ge 0) { [int]$txt.Substring($i + 1) } else { 0 }
  if ($code -ne 200) { Die "Unggah ditolak (HTTP $code): $($txt.Substring(0, [Math]::Max($i, 0)))" }
  Write-Host 'Terunggah. Menunggu ESP32 restart...'

  $ok = $false
  Start-Sleep -Seconds 8
  for ($t = 0; $t -lt 40; $t++) {
    Start-Sleep -Seconds 3
    $p = Api 'GET' '/api/info' $null
    if ($p.code -eq 200 -or $p.code -eq 401) { $ok = $true; break }
  }
  if (-not $ok) { Die 'ESP32 tidak kembali online dalam 2 menit. Firmware lama akan kembali otomatis (rollback) bila booting gagal.' }
  Login
  $inf = Api 'GET' '/api/update/info' $null
  Write-Host "ESP32 online. Info firmware: $($inf.body)" -ForegroundColor Green
}

# ---- 3. Deploy Worker ----
if (-not $SkipWorker) {
  Step 'Uji dan deploy Cloudflare Worker'
  Push-Location (Join-Path $root 'cloud')
  try {
    Run 'uji Worker' { node test/e2e.mjs }
    Run 'wrangler deploy' { npx wrangler deploy }
  } finally { Pop-Location }
}

if (Test-Path $jar) { Remove-Item $jar -Force }
Write-Host "`nSELESAI." -ForegroundColor Green
