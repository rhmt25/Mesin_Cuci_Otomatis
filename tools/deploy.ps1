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
# Kunci identitas: hanya perangkat dengan MAC ini yang menerima password dan firmware (ada ESP lain di jaringan).
$wantMac = ([string]$cfg['DEPLOY_ESP_MAC'] -replace '[^0-9A-Fa-f]', '').ToUpper()
$base = "http://$hostName"   # diganti alamat IP setelah identitas diperiksa
$jar = Join-Path $env:TEMP 'mesin-cuci-deploy.cookies'
$tmpBody = Join-Path $env:TEMP 'mesin-cuci-deploy.json'

function Api($method, $path, $body) {
  # Kata sandi dikirim lewat berkas sementara (tidak tampil di daftar proses), dihapus setelah dipakai.
  $a = @('--noproxy', '*', '-s', '-m', '20', '-b', $jar, '-c', $jar, '-X', $method, '-w', "`n%{http_code}", "$base$path")
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

# Pastikan $hostName benar-benar mesin cuci ini. Tidak mengirim password apa pun.
# Mengunci $base ke satu alamat IP, sehingga nama .local tidak di-resolve ulang di tengah deploy.
function Assert-Device {
  try {
    $ip = [Net.Dns]::GetHostAddresses($hostName) | Where-Object { $_.AddressFamily -eq 'InterNetwork' } |
      Select-Object -First 1 -ExpandProperty IPAddressToString
  } catch { $ip = $null }
  if (-not $ip) { Die "Alamat $hostName tidak ditemukan. Satu jaringan WiFi? Atau isi DEPLOY_ESP_HOST dengan IP." }
  $page = (& curl.exe --noproxy '*' -s -m 20 --compressed "http://$ip/") -join "`n"
  if (-not $page) { Die "ESP32 tidak terjangkau di $hostName ($ip)." }
  if (-not $page.Contains('<title>Mesin Cuci</title>')) { Die "Perangkat di $ip BUKAN dashboard Mesin Cuci. Deploy dibatalkan." }
  $macs = @(Get-NetNeighbor -IPAddress $ip -ErrorAction SilentlyContinue |
    ForEach-Object { ($_.LinkLayerAddress -replace '[^0-9A-Fa-f]', '').ToUpper() } |
    Where-Object { $_ -and $_ -ne '000000000000' } | Sort-Object -Unique)
  if ($macs.Count -ne 1) { Die "MAC perangkat $ip tidak bisa dipastikan (harus satu jaringan lokal dengan komputer ini)." }
  if (-not $wantMac) { Die "DEPLOY_ESP_MAC belum diisi di .env.local. Perangkat di $ip ber-MAC $($macs[0]). Isi nilai itu bila memang mesin cuci Anda." }
  if ($macs[0] -ne $wantMac) { Die "MAC perangkat di $ip adalah $($macs[0]), bukan $wantMac. Deploy dibatalkan agar tidak salah perangkat." }
  $script:base = "http://$ip"
  Write-Host "Perangkat cocok: $hostName = $ip, MAC $($macs[0])" -ForegroundColor Green
}

# ---- 0. Periksa tujuan dulu (sebelum membangun) ----
if (-not $SkipEsp) {
  Step "Periksa identitas ESP32 di $hostName"
  Assert-Device
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
  Step "Cek ulang ESP32, lalu pastikan mesin tidak bekerja"
  Assert-Device   # sekali lagi tepat sebelum password dikirim
  Login
  foreach ($ws in 'cuci', 'pengering') {
    $job = Api 'GET' "/api/job?ws=$ws" $null
    if ($job.code -ne 200 -or $job.body -notmatch '"active"\s*:\s*false') { Die "Workspace $ws sedang bekerja atau sibuk (HTTP $($job.code)). Hentikan Proses dulu." }
  }

  $bin = Join-Path $root '.pio\build\rymcu-esp32-devkitc\firmware.bin'
  if (-not (Test-Path $bin)) { Die "firmware.bin tidak ditemukan: $bin" }
  Step ("Unggah firmware ({0:N0} byte)" -f (Get-Item $bin).Length)
  $out = & curl.exe --noproxy '*' -s -m 240 -H 'Expect:' -b $jar -c $jar -F "file=@$bin;filename=firmware.bin" -w "`n%{http_code}" "$base/api/update"
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
  if (-not $ok) { Die 'ESP32 tidak kembali online dalam 2 menit. Firmware lama akan kembali otomatis (rollback) bila booting gagal. Bila IP-nya berubah, jalankan lagi.' }
  Assert-Device
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
