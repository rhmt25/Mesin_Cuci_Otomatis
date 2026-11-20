'use strict';
const $ = (s, r = document) => r.querySelector(s);
const $$ = (s, r = document) => [...r.querySelectorAll(s)];
let last = null, settings = null, lastState = 'idle';
let conPaused = false, conLines = [], logLines = [], lastSeq = 0;

// ---------------------------------------------------------------- util ----
function toast(msg) {
  const t = $('#toast');
  t.textContent = msg; t.classList.add('show');
  clearTimeout(toast.t); toast.t = setTimeout(() => t.classList.remove('show'), 3200);
}
async function api(path, method = 'GET', body) {
  const opt = { method, headers: {} };
  if (body !== undefined) { opt.headers['Content-Type'] = 'application/json'; opt.body = JSON.stringify(body); }
  let r, lastErr;
  // GET aman diulang bila koneksi tersendat sesaat; POST/DELETE tidak diulang (bisa terkirim dua kali)
  for (let i = 0; i < (method === 'GET' ? 3 : 1); i++) {
    try { r = await fetch(path, opt); break; } catch (e) { lastErr = e; await new Promise(res => setTimeout(res, 500)); }
  }
  if (!r) throw new Error('Koneksi ke mesin terputus. Coba lagi sebentar.');
  if (r.status === 401 && path !== '/api/login') { showLogin(); throw new Error('login'); }
  return r.json();
}
async function cmd(name, args = '') {
  try {
    const r = await api('/api/cmd', 'POST', { cmd: name, args: String(args) });
    toast((r.ok ? '' : '⚠ ') + r.msg);
    if (r.ok) setTimeout(loadSettings, 300);
    return r;
  } catch (e) { if (e.message !== 'login') toast('Gagal: ' + e.message); }
}
const fmt = s => s >= 3600 ? `${Math.floor(s / 3600)}j ${Math.floor(s % 3600 / 60)}m` : s >= 60 ? `${Math.floor(s / 60)}m ${s % 60}d` : `${s}d`;
const esc = s => String(s).replace(/[&<>"]/g, c => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;' }[c]));
const clock = (ms, epoch) => {
  if (epoch) { const d = new Date(epoch * 1000); return d.toTimeString().slice(0, 8); }
  const s = Math.floor(ms / 1000); return `+${s}s`;
};
const fmtMin = s => s >= 3600 ? `${Math.floor(s / 3600)} j ${Math.round(s % 3600 / 60)} mnt` : `${Math.max(1, Math.round(s / 60))} mnt`;

// -------------------------------------------------------------- tema/tab ----
(function theme() {
  const t = localStorage.getItem('theme'); if (t) document.documentElement.dataset.theme = t;
  $('#btnTheme').onclick = () => {
    const cur = document.documentElement.dataset.theme || (matchMedia('(prefers-color-scheme:dark)').matches ? 'dark' : 'light');
    const n = cur === 'dark' ? 'light' : 'dark';
    document.documentElement.dataset.theme = n; localStorage.setItem('theme', n);
  };
})();
$$('#tabs button').forEach(b => b.onclick = () => {
  $$('#tabs button').forEach(x => x.classList.toggle('on', x === b));
  $$('.tab').forEach(x => x.classList.toggle('on', x.id === 'tab-' + b.dataset.tab));
  onTab(b.dataset.tab);
});
function onTab(t) {
  if (t === 'settings') loadSettings();
  if (t === 'wifi') loadWifi();
  if (t === 'relay') renderRelays();
  if (t === 'proses') loadMeta().then(() => { loadSettings(); loadProses(); renderPalette(); });
  if (t === 'manual') loadMeta().then(renderManual);
  if (t === 'history') loadHistory();
  if (t === 'firmware') loadFw();
  if (t === 'system') { renderSys(); loadDevices(); }
  if (t === 'log') renderLog();
  if (t === 'console') renderCon();
}

// --------------------------------------------------------------- login ----
function showLogin() { $('#login').hidden = false; $('#app').hidden = true; clearTimeout(pollTimer); }
$('#loginForm').onsubmit = async e => {
  e.preventDefault();
  const r = await api('/api/login', 'POST', { password: $('#pw').value, remember: $('#remember').checked }).catch(() => ({ ok: false, msg: 'Gagal terhubung' }));
  if (r.ok) { $('#pw').value = ''; start(); } else $('#loginMsg').textContent = r.msg || 'Gagal';
};
$('#btnLogout').onclick = async () => { await api('/api/logout', 'POST'); showLogin(); };

// ---------------------------------------------------------- data-cmd btn ----
document.addEventListener('click', e => {
  const b = e.target.closest('[data-cmd]'); if (!b) return;
  cmd(b.dataset.cmd, b.dataset.args || '');
});
$('#btnStop').onclick = () => {
  if (last && last.state !== 'idle' && !confirm('Hentikan Proses dan matikan semua relay?')) return;
  cmd('stop');
};

// ------------------------------------------------------------- status UI ----
const STATE_TXT = { idle: 'Siap', running: 'Berjalan', paused: 'Dijeda', decide: 'Menunggu keputusan' };
function renderStatus(s) {
  last = s;
  $('#app').hidden = false; $('#login').hidden = true;
  const st = $('#chipState');
  st.textContent = STATE_TXT[s.state] || s.state;
  st.className = 'chip ' + (s.state === 'running' ? 'run' : s.state === 'paused' || s.state === 'decide' ? 'pause' : '');
  const w = $('#chipWifi');
  w.textContent = s.wifi.online ? `📶 ${s.wifi.ssid}` : '📴 offline';
  w.className = 'chip' + (s.wifi.online ? '' : ' off');
  const idle = s.state === 'idle';
  $('#stage').textContent = idle ? 'Siap' : s.stage;
  $('#detail').textContent = idle ? 'Pilih Proses di bawah' : (s.proses ? s.proses + ' · ' : '') + s.detail;
  $('#stepLeft').textContent = idle ? '-' : fmt(s.stepLeft);
  $('#totalLeft').textContent = idle ? '-' : '±' + fmt(s.totalLeft);
  $('#water').textContent = s.water;
  const p = idle ? 0 : s.progress;
  $('#ringFg').style.strokeDashoffset = 327 * (1 - p / 100);
  $('#ringText').textContent = idle ? '—' : p + '%';
  $('#bannerResume').hidden = !s.resume.pending;
  $('#resumeInfo').textContent = s.resume.info;
  const bp = $('#bannerPause');
  bp.hidden = s.state !== 'paused';
  bp.textContent = '⏸ Dijeda oleh pengguna. Tekan Lanjutkan untuk meneruskan.';
  renderDecision(s);
  $('#bannerFault').hidden = !s.fault;
  updateRelays(s);
  $$('.prog, .cpRun').forEach(b => { b.disabled = !idle; });
  const pl = $('#progLock'); pl.hidden = idle;
  pl.textContent = idle ? '' : `Proses baru hanya bisa dimulai saat mesin siap. Sekarang: ${s.proses} (${s.stage}).`;
  lastState = s.state;
  if ($('#tab-system').classList.contains('on')) renderSys();
  if ($('#tab-manual').classList.contains('on')) $('#manRun').disabled = !idle || s.fault;
  if (s.otaBusy) { $('#fwBar').style.width = s.otaProgress + '%'; }
  const key = s.state + '|' + (s.slot || 0);
  if (renderStatus.key !== key) { renderStatus.key = key; if (prosesList) renderHomeProses(); }
}

// Peringatan sebelum "Lanjut" (pemilik mungkin sedang di lokasi dan mengisi/membuang air sendiri)
const LANJUT_WARN = {
  isi_gagal: 'Air mungkin belum cukup. Motor bisa berputar dengan air sedikit. Lanjut hanya bila Anda sudah mengisi air secara manual.',
  buang_gagal: 'Air masih ada di tabung. Tahap berikutnya (mis. Spin) dengan air berat bisa merusak mesin. Lanjut hanya bila Anda sudah membuang air secara manual.',
  spin_penuh: 'Air masih ada di tabung. Spin dengan air berat bisa merusak mesin. Lanjut hanya bila Anda yakin tabung sudah aman.',
  cek_gagal: 'Syarat air belum terpenuhi. Lanjut hanya bila Anda yakin kondisi air sudah aman.',
};
function renderDecision(s) {
  const card = $('#decisionCard');
  const d = s.state === 'decide' ? s.decision : null;
  card.hidden = !d;
  if (!d) { renderDecision.key = ''; return; }
  const key = d.code + '|' + d.opts.join(',');
  if (renderDecision.key === key) { $('#decMsg').textContent = d.msg; return; }
  renderDecision.key = key;
  $('#decMsg').textContent = d.msg;
  $('#decTitle').textContent = `Menunggu keputusan · ${s.proses}`;
  $('#decBtns').innerHTML = d.opts.map((o, i) => `<button class="btn ${o === 'batal' ? 'danger' : o === 'lanjut' || o === 'ulang' || o === 'buang' ? 'primary' : ''}" data-dec="${o}">${esc(d.labels[i])}</button>`).join('');
  $$('#decBtns [data-dec]').forEach(b => b.onclick = async () => {
    const o = b.dataset.dec;
    if (o === 'lanjut' && LANJUT_WARN[d.code] && !confirm('⚠ ' + LANJUT_WARN[d.code] + '\n\nTetap lanjutkan?')) return;
    if (o === 'batal' && !confirm('Batalkan Proses ini dan matikan semua relay?')) return;
    try {
      const r = await api('/api/decide', 'POST', { pilih: o });
      toast((r.ok ? '' : '⚠ ') + r.msg);
      pollOnce();
    } catch (e) { if (e.message !== 'login') toast('Gagal: ' + e.message); }
  });
}

// ------------------------------------------------------------- pengaturan ----
async function loadSettings() {
  try { settings = await api('/api/settings'); } catch { return; }
  const s = settings;
  $('#sNotif').value = ['lengkap', 'ringkas', 'senyap'][s.notif];
  $('#calibNote').textContent = s.calib.nFill ? `Rata-rata isi penuh ${fmt(s.calib.fillSec)} (${s.calib.nFill} sampel). Mode isi "persen" memakai data ini.` : 'Belum ada data kalibrasi: mode isi "persen" memakai sensor sampai ada 1 pengisian penuh dengan mode sensor.';
  const sel = $('#startMode').querySelector('option[value=selesai]');
  if (sel) sel.disabled = !s.timeSynced;
  $('#startNote').textContent = s.timeSynced ? '' : 'Jam belum sinkron (butuh internet): "Selesai pada jam" belum tersedia, gunakan tunda.';
}
$('#btnNotif').onclick = () => cmd('notif', $('#sNotif').value);
$('#btnResetAll').onclick = () => confirm('Reset pengaturan ke default?') && cmd('reset_semua');
$('#btnCalib').onclick = () => confirm('Reset data kalibrasi waktu isi?') && cmd('kalibrasi_air');

// ------------------------------------------------------------------ Relay ----
const RELAYS = [['kiri', 'Motor putar kiri', 'GPIO25'], ['kanan', 'Motor putar kanan', 'GPIO18'], ['isi', 'Valve isi air', 'GPIO17'],
  ['buang', 'Motor valve buang', 'GPIO16'], ['spin', 'Motor spin', 'GPIO4'], ['pewangi', 'Pompa pewangi', 'GPIO13']];
function renderRelays() {
  const host = $('#relayCards');
  if (host.dataset.ready) return;
  host.dataset.ready = '1';
  host.innerHTML = RELAYS.map(([k, label, pin]) => `<div class="card relayCard">
    <div class="info"><span class="led" id="led-${k}"></span><div><b>${label}</b><br><span class="muted">${k} · ${pin}</span><br><span class="lockhint" id="hint-${k}"></span></div></div>
    <div class="row" style="margin:0"><button class="btn primary" data-rl="${k}" data-on="1">ON</button><button class="btn" data-rl="${k}" data-on="0">OFF</button></div></div>`).join('');
  $$('[data-rl]', host).forEach(b => b.onclick = () => cmd('relay', `${b.dataset.rl} ${b.dataset.on === '1' ? 'on' : 'off'}`));
}
function updateRelays(s) {
  renderRelays();
  const on = s.relays || {};
  const busy = s.state !== 'idle';
  const seq = busy && s.proses === 'Kalibrasi relay';
  const lock = $('#relayLock');
  if (s.fault) { lock.hidden = false; lock.textContent = '❌ Relay terkunci karena pelanggaran interlock. Restart mesin untuk membuka kunci.'; }
  else if (busy && !seq) {
    lock.hidden = false;
    lock.textContent = `🔒 Kontrol manual & uji dikunci: ada Proses ${s.state === 'running' ? 'berjalan' : 'yang menunggu/dijeda'} (${s.proses}, ${s.stage}). Hentikan dulu di Beranda.`;
  } else lock.hidden = true;
  const conflict = {
    kiri: on.kanan ? 'Motor kanan sedang aktif, matikan dulu' : on.buang ? 'Valve buang sedang aktif, matikan dulu' : '',
    kanan: on.kiri ? 'Motor kiri sedang aktif, matikan dulu' : on.buang ? 'Valve buang sedang aktif, matikan dulu' : '',
    isi: on.buang ? 'Valve buang sedang aktif, matikan dulu' : '',
    buang: on.isi ? 'Valve isi sedang aktif, matikan dulu' : (on.kiri || on.kanan) ? 'Motor cuci sedang aktif, matikan dulu' : '',
  };
  RELAYS.forEach(([k]) => {
    const l = $('#led-' + k); if (l) l.classList.toggle('on', !!on[k]);
    const onBtn = $(`[data-rl="${k}"][data-on="1"]`), offBtn = $(`[data-rl="${k}"][data-on="0"]`);
    const c = conflict[k] || '';
    const blocked = busy || s.fault;
    if (onBtn) { onBtn.disabled = blocked || !!c || !!on[k]; onBtn.title = c; }
    if (offBtn) offBtn.disabled = blocked || !on[k];
    const h = $('#hint-' + k);
    if (h) h.textContent = !blocked && c && !on[k] ? '🔒 ' + c : '';
  });
  $('#btnSeq').disabled = busy || s.fault;
  const bp = $('#relayProgress');
  bp.hidden = !seq;
  if (seq) bp.textContent = `Uji berurutan: ${s.detail} (langkah ${s.step}/${s.steps})`;
}
$('#btnSeq').onclick = () => cmd('kalibrasi_relay', $('#seqSecs').value || 3);
$('#btnSeqStop').onclick = () => cmd('stop');
$('#btnAllOff').onclick = () => cmd('relay', 'semua off');

// ----------------------------------------------------------------- WiFi ----
async function loadWifi() {
  const w = await api('/api/wifi');
  $('#wifiInfo').innerHTML = `<b>Hotspot:</b> ${w.apOn ? esc(w.ap) + ' AKTIF (192.168.4.1)' : 'nonaktif (menyala otomatis bila semua WiFi gagal)'}<br><b>Jaringan:</b> ${w.online ? esc(w.ssid) + ' · ' + esc(w.ip) + ' · ' + w.rssi + ' dBm' : 'offline'}`;
  $('#wifiList').innerHTML = w.networks.map(n => `<div class="card wifirow"><div><b>${n.i}. ${esc(n.ssid)}</b> ${n.active ? '✅' : ''}<br><span class="muted">${n.secured ? '••••' : 'terbuka'}</span></div>
    <div class="row" style="margin:0"><button class="btn" data-up="${n.i}">↑</button><button class="btn" data-dn="${n.i}">↓</button><button class="btn danger" data-del="${n.i}">Hapus</button></div></div>`).join('') || '<p class="muted">Belum ada WiFi.</p>';
  $$('[data-del]').forEach(b => b.onclick = async () => { await cmd('wifi_hapus', b.dataset.del); loadWifi(); });
  $$('[data-up]').forEach(b => b.onclick = async () => { const i = +b.dataset.up; if (i > 1) { await cmd('wifi_urutan', `${i} ${i - 1}`); loadWifi(); } });
  $$('[data-dn]').forEach(b => b.onclick = async () => { const i = +b.dataset.dn; if (i < w.networks.length) { await cmd('wifi_urutan', `${i} ${i + 1}`); loadWifi(); } });
}
$('#wifiForm').onsubmit = async e => {
  e.preventDefault();
  const r = await api('/api/wifi', 'POST', { ssid: $('#wSsid').value, pass: $('#wPass').value });
  toast((r.ok ? '' : '⚠ ') + r.msg);
  if (r.ok) { $('#wSsid').value = ''; $('#wPass').value = ''; setTimeout(loadWifi, 400); }
};

// ------------------------------------------------------------ Log & konsol ----
const LV = ['DEBUG', 'INFO', 'STEP', 'WARN', 'ERROR', 'DONE', 'NOTIF'];
function lineHtml(e) { return `<div class="l-${e.l}">${clock(e.t, e.e)} ${(LV[e.l] || '?').padEnd(5)} ${esc(e.m)}</div>`; }
function addEntries(list, live) {
  let alertEntry = null;
  for (const e of list) {
    if (e.s <= lastSeq) continue;
    lastSeq = e.s; logLines.push(e); conLines.push(e);
    if (live && (e.l === 3 || e.l === 4 || e.l === 6)) alertEntry = e;   // WARN / ERROR / NOTIF dari mesin
  }
  if (alertEntry) toast((alertEntry.l === 4 ? '❌ ' : alertEntry.l === 6 ? '📣 ' : '⚠ ') + alertEntry.m);
  if (logLines.length > 400) logLines.splice(0, logLines.length - 400);
  if (conLines.length > 600) conLines.splice(0, conLines.length - 600);
  if ($('#tab-log').classList.contains('on')) renderLog();
  if ($('#tab-console').classList.contains('on') && !conPaused) renderCon();
}
function renderLog() {
  const min = +$('#logLevel').value, box = $('#logBox');
  const stick = box.scrollTop + box.clientHeight >= box.scrollHeight - 30;
  box.innerHTML = logLines.filter(e => e.l >= min).map(lineHtml).join('');
  if (stick) box.scrollTop = box.scrollHeight;
}
function renderCon() {
  const box = $('#conBox');
  box.innerHTML = conLines.map(lineHtml).join('');
  box.scrollTop = box.scrollHeight;
}
$('#logLevel').onchange = renderLog;
$('#conPause').onclick = e => { conPaused = !conPaused; e.target.textContent = conPaused ? '▶ Lanjut' : '⏸ Pause'; if (!conPaused) renderCon(); };
$('#conClear').onclick = () => { conLines = []; renderCon(); };
$('#conDl').onclick = () => {
  const txt = conLines.map(e => `${clock(e.t, e.e)} ${LV[e.l]} ${e.m}`).join('\n');
  const a = document.createElement('a'); a.href = URL.createObjectURL(new Blob([txt], { type: 'text/plain' }));
  a.download = 'konsol-mesin-cuci.txt'; a.click();
};
$('#conTrace').onclick = async () => {
  const r = await api('/api/console');
  $('#conBox').textContent = `Alasan reset terakhir: ${r.reset}\n--- ±40 baris terakhir sebelum restart ---\n${r.trace || '(tidak ada)'}`;
};
$('#conForm').onsubmit = async e => {
  e.preventDefault();
  const v = $('#conIn').value.trim(); if (!v) return;
  $('#conIn').value = '';
  await api('/api/console', 'POST', { line: v }).catch(() => { });
  pollOnce();
};
async function pollLogs() {
  try { const r = await api('/api/poll?since=' + lastSeq); addEntries(r.log.entries); return r.log.entries.length; } catch { return 0; }
}
// Server membatasi 30 entri per balasan (hemat memori); ambil lanjutannya sampai habis.
async function fetchLogsAll() { for (let i = 0; i < 6; i++) { if ((await pollLogs()) < 30) break; } }

// -------------------------------------------------------- riwayat/fw/sistem ----
async function loadHistory() {
  const r = await api('/api/history');
  const st = r.stats;
  $('#statsBox').innerHTML = `<b>Total Proses selesai:</b> ${st.cycles}<br><b>Motor cuci menyala:</b> ${(st.motorSec / 3600).toFixed(1)} jam<br><b>Rata-rata isi penuh:</b> ${st.avgFillSec ? fmt(st.avgFillSec) : '-'}`;
  $('#histList').innerHTML = r.history.map(h => `<div class="hist"><div><b>${esc(h.proses)}</b><br><span class="muted">${h.start ? new Date(h.start * 1000).toLocaleString('id-ID') : '-'}</span></div><div style="text-align:right">${fmt(h.dur)}<br><span class="muted">${h.result}</span></div></div>`).join('') || '<p class="muted">Belum ada riwayat.</p>';
}
async function loadFw() {
  const i = await api('/api/update/info');
  $('#fwInfo').innerHTML = `<b>Versi aktif:</b> ${esc(i.version)} · ${esc(i.build)}<br><b>Partisi:</b> ${i.running} (cadangan: ${i.other})<br><b>Ukuran firmware:</b> ${(i.sketchSize / 1024).toFixed(0)} KB · ruang ${(i.freeSpace / 1024).toFixed(0)} KB<br><b>Versi sebelumnya:</b> ${i.canRollback ? esc(i.previousBuild || 'tersedia') : 'tidak ada'}`;
  $('#btnRollback').disabled = !i.canRollback;
}
$('#btnFlash').onclick = () => {
  const f = $('#fwFile').files[0];
  if (!f) return toast('Pilih file .bin dulu');
  if (last && last.state !== 'idle') return toast('Mesin sedang bekerja. Hentikan Proses dulu.');
  if (!confirm(`Flash ${f.name} (${(f.size / 1024).toFixed(0)} KB)? Mesin akan restart.`)) return;
  const fd = new FormData(); fd.append('firmware', f, f.name);
  const x = new XMLHttpRequest(); x.open('POST', '/api/update');
  x.upload.onprogress = e => { if (e.lengthComputable) { const p = Math.round(e.loaded / e.total * 100); $('#fwBar').style.width = p + '%'; $('#fwMsg').textContent = `Mengunggah ${p}%`; } };
  x.onload = () => { let r = {}; try { r = JSON.parse(x.responseText); } catch { } $('#fwMsg').textContent = r.msg || 'Selesai'; toast(r.msg || 'Selesai'); if (r.ok) setTimeout(() => location.reload(), 9000); };
  x.onerror = () => { $('#fwMsg').textContent = 'Koneksi terputus (mesin mungkin sedang restart).'; };
  x.send(fd);
};
$('#btnRollback').onclick = async () => {
  if (!confirm('Kembali ke firmware sebelumnya dan restart?')) return;
  const r = await api('/api/rollback', 'POST'); toast(r.msg);
};
function renderSys() {
  if (!last) return;
  const s = last;
  $('#sysInfo').innerHTML = `<b>Firmware:</b> v${esc(s.fw)}<br><b>Uptime:</b> ${fmt(s.uptime)}<br><b>Heap bebas:</b> ${(s.heap / 1024).toFixed(0)} KB<br><b>Alasan reset terakhir:</b> ${esc(s.reset)}<br><b>WiFi:</b> ${s.wifi.online ? esc(s.wifi.ssid) + ' · ' + esc(s.wifi.ip) : 'offline'}<br><b>Hotspot:</b> ${s.wifi.apOn ? esc(s.wifi.ap) + ' AKTIF (' + s.wifi.apClients + ' klien)' : 'nonaktif'}<br><b>Jam:</b> ${esc(s.wifi.time)}<br><b>Notifikasi:</b> ${esc(s.notif)}<br><b>Penyimpanan Proses:</b> ${s.fs ? 'siap' : '<span class="muted">tidak tersedia</span>'}`;
  const rm = s.remote || {};
  const ri = $('#remoteInfo');
  if (ri) ri.textContent = !rm.configured ? 'Belum dikonfigurasi (isi REMOTE_URL dan REMOTE_DEVICE_TOKEN di secrets.h).' : !rm.enabled ? 'Dinonaktifkan.' : rm.connected ? '✅ Terhubung ke server jarak jauh.' : '⏳ Mencoba menyambung ke server…';
}
$('#btnPw').onclick = async () => { const r = await cmd('web_password', $('#newPw').value); if (r && r.ok) { $('#newPw').value = ''; toast('Password diganti. Silakan login ulang.'); setTimeout(showLogin, 800); } };
$('#btnRestart').onclick = () => confirm('Restart mesin?') && cmd('restart');
// ---- cadangkan & pulihkan (khusus lokal) ----
$('#btnBackup').onclick = () => { location.href = '/api/backup' + ($('#bkWifi').checked ? '?wifi=1' : ''); };
function readFileText(f) {
  return new Promise((res, rej) => { const fr = new FileReader(); fr.onload = () => res(fr.result); fr.onerror = () => rej(fr.error); fr.readAsText(f); });
}
$('#restoreFile').onchange = async e => {
  const f = e.target.files[0]; e.target.value = '';
  const msg = $('#restoreMsg');
  if (!f) return;
  if (f.size > 12288) { msg.textContent = '⚠ Berkas terlalu besar (maks 12 KB).'; return; }
  let data;
  try { data = JSON.parse(await readFileText(f)); } catch { msg.textContent = '⚠ Berkas bukan JSON yang valid.'; return; }
  const dry = await api('/api/restore?dry=1', 'POST', data).catch(() => null);
  if (!dry) { msg.textContent = '⚠ Gagal memeriksa berkas.'; return; }
  if (!dry.ok) { msg.textContent = '⚠ ' + dry.msg; return; }
  if (!confirm(`Pulihkan dari cadangan?\n${dry.msg}\n\nPengaturan dan daftar WiFi saat ini akan diganti. Proses tidak ikut (ekspor/impor di tab Proses).`)) { msg.textContent = 'Dibatalkan.'; return; }
  const r = await api('/api/restore', 'POST', data).catch(() => null);
  msg.textContent = r && r.ok ? '✅ ' + r.msg : '⚠ ' + (r ? r.msg : 'Gagal memulihkan.');
  if (r && r.ok) { loadSettings(); loadWifi(); toast('Pemulihan selesai'); }
};

// =========================================================== Tahap & Proses ====
// Katalog Tahap datang dari firmware (/api/tahap/meta): UI tidak menyimpan rentang/bawaan sendiri.
let META = [];
const ICON = { isi: '🚰', buang: '🚿', putar: '🔄', soak: '🛁', rendam: '💧', spin: '🌀', pewangi: '🌼', tunggu: '⏳', cek: '🔎', tindakan: '✋', notif: '📣', buang_manual: '🚿' };
async function loadMeta() {
  if (META.length) return;
  try { const r = await api('/api/tahap/meta'); META = r.types || []; } catch { /* diulang saat tab dibuka lagi */ }
}
const typeMeta = t => META.find(x => x.t === t);
let uidSeq = 1;
function newItem(t, vals) {
  const m = typeMeta(t), v = {};
  m.fields.forEach(f => { v[f.k] = f.def; });
  return { uid: uidSeq++, t, v: { ...v, ...(vals || {}) } };
}
const applies = (f, v) => !f.when || f.when.includes(v.mode);
const hms = s => `${String(Math.floor(s / 3600)).padStart(2, '0')}:${String(Math.floor(s % 3600 / 60)).padStart(2, '0')}:${String(s % 60).padStart(2, '0')}`;

function fieldHtml(f, it) {
  const val = it.v[f.k], base = `data-uid="${it.uid}" data-k="${f.k}"`;
  let ctl, unit = '';
  if (f.kind === 'enum') ctl = `<select ${base}>${f.opts.map((o, i) => `<option value="${o}"${o === val ? ' selected' : ''}>${esc(f.optLabels[i])}</option>`).join('')}</select>`;
  else if (f.kind === 'choice') ctl = `<select ${base}>${f.opts.map(o => `<option value="${o}"${+o === +val ? ' selected' : ''}>${o}${f.unit || ''}</option>`).join('')}</select>`;
  else if (f.kind === 'cup') { ctl = `<input type="number" ${base} min="0.5" max="10" step="0.5" inputmode="decimal" value="${val}">`; unit = ' (cup)'; }
  else if (f.kind === 'text') ctl = `<input type="text" ${base} maxlength="${f.max}" value="${esc(val)}">`;
  else if (f.fmt === 'hms') {
    ctl = '<span class="tri">' + [['h', Math.floor(val / 3600), 23], ['m', Math.floor(val % 3600 / 60), 59], ['s', val % 60, 59]]
      .map(([p, x, mx]) => `<input type="number" ${base} data-hms="${p}" min="0" max="${mx}" inputmode="numeric" value="${x}" aria-label="${p === 'h' ? 'jam' : p === 'm' ? 'menit' : 'detik'}">`).join('') + '</span>';
    unit = ' (jam : menit : detik)';
  } else { ctl = `<input type="number" ${base} min="${f.min}" max="${f.max}" inputmode="numeric" value="${val}">`; unit = f.unit ? ` (${f.unit})` : ''; }
  return `<label data-f="${f.k}"${applies(f, it.v) ? '' : ' hidden'}>${esc(f.label)}${unit}${ctl}</label>`;
}
const fieldsHtml = it => typeMeta(it.t).fields.map(f => fieldHtml(f, it)).join('');

// Mengambil nilai dari kontrol ke item. Mengembalikan true bila event ini milik form Tahap.
function handleField(e, items) {
  const el = e.target.closest('[data-k]'); if (!el) return false;
  const it = items.find(x => x.uid === +el.dataset.uid); if (!it) return false;
  const f = typeMeta(it.t).fields.find(x => x.k === el.dataset.k);
  if (f.kind === 'enum' || f.kind === 'text') it.v[f.k] = el.value;
  else if (f.kind === 'choice' || f.kind === 'cup') it.v[f.k] = parseFloat(el.value);
  else if (el.dataset.hms) {
    const [h, m, s] = $$('[data-hms]', el.closest('label')).map(i => parseInt(i.value, 10) || 0);
    it.v[f.k] = h * 3600 + m * 60 + s;
  } else it.v[f.k] = parseInt(el.value, 10);
  const body = el.closest('.fields');
  typeMeta(it.t).fields.forEach(g => { const lab = $(`[data-f="${g.k}"]`, body); if (lab) lab.hidden = !applies(g, it.v); });
  return true;
}
// Pesan galat bahasa Indonesia, atau '' bila valid (firmware tetap memvalidasi ulang)
function validateItem(it) {
  const m = typeMeta(it.t);
  for (const f of m.fields) {
    if (!applies(f, it.v)) continue;
    const v = it.v[f.k], pre = `${m.label}: ${f.label} harus `;
    if (f.kind === 'text') { if (!String(v).trim() || String(v).length > f.max) return pre + `diisi (1-${f.max} karakter)`; }
    else if (f.kind === 'enum') { if (!f.opts.includes(v)) return pre + 'salah satu pilihan'; }
    else if (f.kind === 'choice') { if (!f.opts.includes(+v)) return pre + 'salah satu dari ' + f.opts.join('/'); }
    else if (f.kind === 'cup') { if (!(v >= 0.5 && v <= 10 && Math.abs(v * 2 - Math.round(v * 2)) < 1e-6)) return pre + '0,5 - 10 cup (kelipatan 0,5)'; }
    else if (!(Number.isInteger(v) && v >= f.min && v <= f.max)) return pre + (f.fmt === 'hms' ? `${hms(f.min)} - ${hms(f.max)}` : `${f.min} - ${f.max}${f.unit ? ' ' + f.unit : ''}`);
  }
  return '';
}
const itemToApi = it => { const o = { t: it.t }; typeMeta(it.t).fields.forEach(f => { if (applies(f, it.v)) o[f.k] = it.v[f.k]; }); return o; };
const itemFromApi = o => newItem(o.t, Object.fromEntries(Object.entries(o).filter(([k]) => k !== 't')));

// ------------------------------------------------------------------ Manual ----
const man = { t: 'isi', item: null };
function renderManual() {
  if (!META.length) { $('#manForm').innerHTML = '<p class="muted">Memuat daftar Tahap…</p>'; return; }
  const sel = $('#manType');
  if (!sel.options.length) sel.innerHTML = META.map(m => `<option value="${m.t}">${ICON[m.t] || ''} ${esc(m.label)}${m.manual ? ' (hanya Manual)' : ''}</option>`).join('');
  if (!man.item || man.item.t !== sel.value) { man.item = newItem(sel.value); }
  $('#manForm').innerHTML = `<div class="fields">${fieldsHtml(man.item)}</div>`;
  $('#manHint').hidden = sel.value !== 'buang_manual';
  $('#manRun').disabled = !(last && last.state === 'idle') || !!(last && last.fault);
}
$('#manType').onchange = () => { man.item = null; renderManual(); };
$('#manForm').addEventListener('input', e => { handleField(e, [man.item]); });
$('#manForm').addEventListener('change', e => { handleField(e, [man.item]); });
$('#manRun').onclick = async () => {
  if (!man.item) return;
  const err = validateItem(man.item);
  if (err) return toast('⚠ ' + err);
  if (man.item.t === 'buang_manual' && !confirm('Buang manual menarik valve selama waktu yang diatur TANPA membaca sensor. Pastikan Anda memantau. Lanjut?')) return;
  try {
    const r = await api('/api/run', 'POST', { manual: itemToApi(man.item) });
    toast((r.ok ? '' : '⚠ ') + r.msg);
    pollOnce();
  } catch (e) { if (e.message !== 'login') toast('Gagal: ' + e.message); }
};

// ------------------------------------------------------------------ Proses ----
let prosesList = null;
const ed = { slot: 0, items: [], locked: false, warnings: [], est: 0 };
function renderPalette() {
  const host = $('#pcPalette');
  if (!META.length || host.dataset.ready) return;
  host.dataset.ready = '1';
  host.innerHTML = META.filter(m => !m.manual).map(m => `<button class="btn" data-add="${m.t}">${ICON[m.t] || ''} ${esc(m.label)}</button>`).join('');
  $$('[data-add]', host).forEach(b => b.onclick = () => {
    if (ed.locked) return;
    if (ed.items.length >= (prosesList ? prosesList.maxTahap : 40)) return toast('⚠ Maksimal 40 Tahap per Proses');
    ed.items.push(newItem(b.dataset.add)); renderStack(); scheduleDry();
    const st = $('#pcStack'); st.lastElementChild && st.lastElementChild.scrollIntoView({ block: 'nearest' });
  });
}
function rowHtml(it, i) {
  const m = typeMeta(it.t);
  return `<div class="crow" data-uid="${it.uid}"><span class="handle" title="Tahan &amp; geser untuk menukar urutan">☰</span>
    <div class="cbody"><div class="ctitle"><span class="num">${i + 1}</span> ${ICON[it.t] || ''} <b>${esc(m.label)}</b></div><div class="fields">${fieldsHtml(it)}</div></div>
    <div class="cacts"><button data-act="up" title="Naik">▲</button><button data-act="down" title="Turun">▼</button><button data-act="dup" title="Gandakan">⧉</button><button data-act="del" title="Hapus">🗑</button></div></div>`;
}
function renderStack() {
  $('#pcStack').innerHTML = ed.items.map(rowHtml).join('');
  $('#pcEmpty').hidden = ed.items.length > 0;
  renderEditorState();
}
function renumber() { $$('#pcStack .crow').forEach((r, i) => { $('.num', r).textContent = i + 1; }); }
function renderEditorState() {
  $('#pcLock').hidden = !ed.locked;
  $('#pcLock').textContent = ed.locked ? '🔒 Proses ini sedang dipakai (berjalan, dijeda, menunggu keputusan, atau pemulihan tertunda). Tekan Stop di Beranda dulu untuk mengubah atau menghapusnya.' : '';
  ['pcSave', 'pcName'].forEach(id => { $('#' + id).disabled = ed.locked; });
  $$('#pcStack input, #pcStack select, #pcStack button, #pcPalette button').forEach(x => { x.disabled = ed.locked; });
  $('#pcSummary').textContent = ed.items.length ? `${ed.items.length} Tahap${ed.est ? ' · perkiraan ± ' + fmtMin(ed.est) : ''}` : '';
  const w = $('#pcWarn');
  w.hidden = !ed.warnings.length;
  w.innerHTML = ed.warnings.length ? '⚠ <b>Peringatan</b> (boleh disimpan setelah konfirmasi):<ul>' + ed.warnings.map(x => `<li>${esc(x)}</li>`).join('') + '</ul>' : '';
}
function bodyOf() { return { name: $('#pcName').value.trim(), tahap: ed.items.map(itemToApi) }; }
function localError() {
  if (!$('#pcName').value.trim()) return 'Nama Proses wajib diisi.';
  if (!ed.items.length) return 'Tambahkan minimal satu Tahap.';
  for (let i = 0; i < ed.items.length; i++) { const e = validateItem(ed.items[i]); if (e) return `Tahap ${i + 1}: ${e}`; }
  return '';
}
// Uji coba ke firmware (tanpa menyimpan): peringatan urutan + perkiraan durasi
let dryTimer = null;
function scheduleDry() {
  clearTimeout(dryTimer);
  dryTimer = setTimeout(async () => {
    if (localError() || ed.locked) { ed.warnings = []; ed.est = 0; renderEditorState(); return; }
    try { const r = await api('/api/proses', 'POST', { ...bodyOf(), slot: ed.slot, dry: true }); if (r.ok) { ed.warnings = r.warnings || []; ed.est = r.est || 0; renderEditorState(); } } catch { /* tidak penting */ }
  }, 500);
}
$('#pcStack').addEventListener('input', e => { if (handleField(e, ed.items)) scheduleDry(); });
$('#pcStack').addEventListener('change', e => { if (handleField(e, ed.items)) scheduleDry(); });
$('#pcName').addEventListener('input', scheduleDry);
$('#pcStack').addEventListener('click', e => {
  const b = e.target.closest('[data-act]'); if (!b || ed.locked) return;
  const uid = +b.closest('.crow').dataset.uid, i = ed.items.findIndex(x => x.uid === uid);
  if (b.dataset.act === 'del') ed.items.splice(i, 1);
  else if (b.dataset.act === 'up' && i > 0) [ed.items[i - 1], ed.items[i]] = [ed.items[i], ed.items[i - 1]];
  else if (b.dataset.act === 'down' && i < ed.items.length - 1) [ed.items[i + 1], ed.items[i]] = [ed.items[i], ed.items[i + 1]];
  else if (b.dataset.act === 'dup') { if (ed.items.length >= 40) return toast('⚠ Maksimal 40 Tahap per Proses'); ed.items.splice(i + 1, 0, { uid: uidSeq++, t: ed.items[i].t, v: { ...ed.items[i].v } }); }
  renderStack(); scheduleDry();
});
// Geser untuk menukar urutan (pointer events: jalan di mouse & layar sentuh)
$('#pcStack').addEventListener('pointerdown', e => {
  const h = e.target.closest('.handle'); if (!h || ed.locked) return;
  const list = $('#pcStack'), row = h.closest('.crow');
  e.preventDefault(); h.setPointerCapture(e.pointerId); row.classList.add('drag');
  const move = ev => {
    const others = [...list.children].filter(r => r !== row);
    const target = others.find(r => { const b = r.getBoundingClientRect(); return ev.clientY < b.top + b.height / 2; });
    if (target) { if (row.nextElementSibling !== target) list.insertBefore(row, target); } else list.appendChild(row);
    if (ev.clientY < 70) window.scrollBy(0, -12); else if (ev.clientY > innerHeight - 70) window.scrollBy(0, 12);
  };
  const up = () => {
    h.removeEventListener('pointermove', move); h.removeEventListener('pointerup', up); h.removeEventListener('pointercancel', up);
    row.classList.remove('drag');
    const ids = [...list.children].map(r => +r.dataset.uid);
    ed.items = ids.map(id => ed.items.find(x => x.uid === id));
    renumber(); scheduleDry();
  };
  h.addEventListener('pointermove', move); h.addEventListener('pointerup', up); h.addEventListener('pointercancel', up);
});
function edClear(title) {
  ed.slot = 0; ed.items = []; ed.locked = false; ed.warnings = []; ed.est = 0;
  $('#pcName').value = ''; $('#pcError').hidden = true; $('#pcTitle').textContent = title || 'Proses baru';
  renderStack();
}
$('#pcReset').onclick = () => { if (!ed.items.length || confirm('Kosongkan editor?')) edClear(); };
async function pcSave(asNew) {
  const err = $('#pcError'); err.hidden = true;
  const fail = m => { err.textContent = m; err.hidden = false; err.scrollIntoView({ block: 'nearest' }); };
  const le = localError();
  if (le) return fail(le);
  const slot = asNew ? 0 : ed.slot;
  let dry;
  try { dry = await api('/api/proses', 'POST', { ...bodyOf(), slot, dry: true }); } catch (e) { return; }
  if (!dry.ok) return fail(dry.msg);
  if (dry.warnings && dry.warnings.length && !confirm('Peringatan:\n- ' + dry.warnings.join('\n- ') + '\n\nTetap simpan?')) return;
  let r; try { r = await api('/api/proses', 'POST', { ...bodyOf(), slot }); } catch (e) { return; }
  if (!r.ok) return fail(r.msg);
  ed.slot = r.slot; toast('✅ ' + r.msg);
  $('#pcTitle').textContent = `Mengedit: ${$('#pcName').value.trim()} (slot ${r.slot})`;
  loadProses();
}
$('#pcSave').onclick = () => pcSave(false);
$('#pcSaveNew').onclick = () => pcSave(true);

async function pcLoad(slot, asCopy) {
  let r; try { r = await api('/api/proses?slot=' + slot); } catch { return; }
  if (!r.proses) return toast('⚠ ' + (r.msg || 'Proses tidak bisa dimuat'));
  await loadMeta();
  ed.slot = asCopy ? 0 : slot;
  ed.locked = asCopy ? false : !!r.locked;
  ed.items = r.proses.tahap.map(itemFromApi);
  ed.warnings = r.warnings || []; ed.est = r.est || 0;
  $('#pcName').value = asCopy ? r.proses.name + ' (salinan)' : r.proses.name;
  $('#pcTitle').textContent = asCopy ? 'Proses baru (salinan)' : `Mengedit: ${r.proses.name} (slot ${slot})`;
  $('#pcError').hidden = true;
  renderStack(); $('#pcEditor').scrollIntoView({ behavior: 'smooth', block: 'start' });
}
async function pcExport(slot) {
  let r; try { r = await api('/api/proses/export?slot=' + slot); } catch { return; }
  if (!r.tahap) return toast('⚠ ' + (r.msg || 'Gagal mengekspor'));
  const a = document.createElement('a');
  a.href = URL.createObjectURL(new Blob([JSON.stringify(r, null, 1)], { type: 'application/json' }));
  a.download = 'proses-' + String(r.name).replace(/[^\w\-]+/g, '_') + '.json'; a.click();
}
$('#pcImportFile').onchange = async e => {
  const f = e.target.files[0]; e.target.value = '';
  if (!f) return;
  if (f.size > 8192) return toast('⚠ Berkas terlalu besar (maks 8 KB)');
  let data; try { data = JSON.parse(await readFileText(f)); } catch { return toast('⚠ Berkas bukan JSON yang valid'); }
  const empty = prosesList ? prosesList.slots.findIndex(s => !s) + 1 : 0;
  if (!empty) return toast('⚠ Penyimpanan penuh. Hapus satu Proses dulu.');
  const r = await api('/api/proses/import?slot=' + empty, 'POST', data).catch(() => null);
  toast(r && r.ok ? '✅ Diimpor ke slot ' + r.slot : '⚠ ' + (r ? r.msg : 'Gagal mengimpor'));
  loadProses();
};
async function pcDelete(slot, name) {
  if (!confirm(`Hapus Proses "${name}"?`)) return;
  const d = await api('/api/proses?slot=' + slot, 'DELETE').catch(() => null);
  if (!d) return;
  toast(d.ok ? '🗑 ' + d.msg : '⚠ ' + d.msg);
  if (d.ok && ed.slot === slot) edClear();
  loadProses();
}
async function loadProses() {
  let r; try { r = await api('/api/proses'); } catch { return; }
  prosesList = r;
  const slots = r.slots.map((s, i) => s ? { ...s, slot: i + 1 } : null).filter(Boolean);
  $('#pcList').innerHTML = slots.map(s => s.bad
    ? `<div class="card cpcard"><div class="info"><b>${s.slot}. (berkas rusak)</b></div><div class="row"><button class="btn danger sm" data-pdel="${s.slot}" data-name="berkas rusak">Hapus</button></div></div>`
    : `<div class="card cpcard"><div class="info"><b>${s.slot}. ${esc(s.name)}</b>${s.locked ? ' <span class="chip run">dipakai</span>' : ''}<br><span class="muted">${s.count} Tahap · ± ${fmtMin(s.est)}</span></div>
      <div class="row"><button class="btn sm" data-pedit="${s.slot}">✎ ${s.locked ? 'Lihat' : 'Edit'}</button><button class="btn sm" data-pcopy="${s.slot}">⧉ Salin</button><button class="btn sm" data-pexp="${s.slot}">⬇ Ekspor</button><button class="btn danger sm" data-pdel="${s.slot}" data-name="${esc(s.name)}" ${s.locked ? 'disabled title="Sedang dipakai"' : ''}>Hapus</button></div></div>`
  ).join('') || '<p class="muted">Belum ada Proses. Susun di editor di bawah.</p>';
  $('#pcCount').textContent = `${slots.length}/${r.max}`;
  $$('#pcList [data-pedit]').forEach(b => b.onclick = () => pcLoad(+b.dataset.pedit, false));
  $$('#pcList [data-pcopy]').forEach(b => b.onclick = () => pcLoad(+b.dataset.pcopy, true));
  $$('#pcList [data-pexp]').forEach(b => b.onclick = () => pcExport(+b.dataset.pexp));
  $$('#pcList [data-pdel]').forEach(b => b.onclick = () => pcDelete(+b.dataset.pdel, b.dataset.name));
  renderHomeProses();
  if (ed.slot) { const cur = slots.find(s => s.slot === ed.slot); if (cur && ed.locked !== !!cur.locked) { ed.locked = !!cur.locked; renderEditorState(); } }
}

// ---------------------------------------------------------- Beranda: mulai ----
function startBody(slot) {
  const mode = $('#startMode').value, body = { slot };
  if (mode === 'tunda') {
    const v = $('#startTunda').value || '00:30:00';
    body.tunda = v.length === 5 ? v + ':00' : v;
  } else if (mode === 'selesai') {
    const v = $('#startSelesai').value;
    if (!v) { toast('⚠ Isi jam selesai dulu'); return null; }
    body.selesai = v;
  }
  return body;
}
function renderHomeProses() {
  if (!prosesList) return;
  const idle = !last || last.state === 'idle';
  const slots = prosesList.slots.map((s, i) => s ? { ...s, slot: i + 1 } : null).filter(Boolean);
  $('#homeEmpty').hidden = slots.length > 0;
  $('#startOpts').hidden = slots.length === 0;
  $('#prosesGrid').innerHTML = slots.map(s => s.bad
    ? `<div class="card prog" aria-disabled="true"><b>⚠ Slot ${s.slot}</b><small>berkas rusak (hapus di tab Proses)</small></div>`
    : `<button class="card prog cpRun" data-run="${s.slot}" ${idle ? '' : 'disabled'}><b>▶ ${esc(s.name)}</b><small>${s.count} Tahap · ± ${fmtMin(s.est)}</small></button>`).join('');
  $$('#prosesGrid [data-run]').forEach(b => b.onclick = async () => {
    const body = startBody(+b.dataset.run); if (!body) return;
    try { const r = await api('/api/run', 'POST', body); toast((r.ok ? '' : '⚠ ') + r.msg); pollOnce(); } catch (e) { if (e.message !== 'login') toast('Gagal: ' + e.message); }
  });
}
$('#startMode').onchange = () => {
  const m = $('#startMode').value;
  $('#startTunda').hidden = m !== 'tunda'; $('#startSelesai').hidden = m !== 'selesai';
};
$('#goProses').onclick = () => $('#tabs [data-tab=proses]').click();

// ---------------------------------------------------- perangkat yang diingat ----
const fmtDate = e => e ? new Date(e * 1000).toLocaleString('id-ID', { dateStyle: 'medium', timeStyle: 'short' }) : '-';
async function loadDevices() {
  let r; try { r = await api('/api/devices'); } catch { return; }
  $('#devHint').textContent = r.temporary
    ? 'Sesi ini TIDAK diingat (login ulang saat browser ditutup). Centang "Ingat perangkat ini" saat login.'
    : `Maksimal ${r.max} perangkat. Bila penuh, yang terlama otomatis dihapus. Perangkat tetap masuk sampai kamu menghapusnya.`;
  $('#devList').innerHTML = r.devices.map(d => `<div class="wifirow" style="margin:8px 0"><div><b>${esc(d.name)}</b> ${d.current ? '<span class="chip run">Perangkat ini</span>' : ''}<br><span class="muted">Masuk ${fmtDate(d.created)} · terakhir aktif ${fmtDate(d.lastSeen)}</span></div>
    <button class="btn danger" data-dev="${d.id}" data-cur="${d.current ? 1 : 0}">Hapus</button></div>`).join('') || '<p class="muted">Belum ada perangkat yang diingat.</p>';
  $$('#devList [data-dev]').forEach(b => b.onclick = async () => {
    if (!confirm(b.dataset.cur === '1' ? 'Hapus perangkat INI? Kamu akan keluar.' : 'Hapus perangkat ini dari daftar?')) return;
    const d = await api('/api/devices?id=' + b.dataset.dev, 'DELETE');
    toast(d.ok ? '✅ ' + d.msg : '⚠ ' + d.msg);
    if (d.self) showLogin(); else loadDevices();
  });
}
$('#btnDevOthers').onclick = async () => {
  if (!confirm('Keluarkan semua perangkat lain?')) return;
  const d = await api('/api/devices?others=1', 'DELETE'); toast(d.msg); loadDevices();
};

// ---------------------------------------------------------------- koneksi ----
// Tanpa WebSocket (hemat memori ESP32): polling status + log tiap 2 dtk, 10 dtk saat tab tersembunyi.
let pollTimer = null, pollBusy = false;
async function pollOnce() {
  if (pollBusy) return;
  pollBusy = true;
  try {
    const r = await api('/api/poll?since=' + lastSeq);
    if (r.status) renderStatus(r.status);
    updateRemoteBanner(r.remote);
    addEntries(r.log.entries, true);
  } catch (e) { /* koneksi tersendat: dicoba lagi di putaran berikutnya */ } finally { pollBusy = false; }
}
function schedulePoll() {
  clearTimeout(pollTimer);
  pollTimer = setTimeout(async () => { if (!$('#app').hidden) await pollOnce(); schedulePoll(); }, document.hidden ? 10000 : 2000);
}
document.addEventListener('visibilitychange', () => { if (!document.hidden && !$('#app').hidden) { pollOnce(); schedulePoll(); } });
// Mode jarak jauh (Worker Cloudflare): kode UI sama, tetapi fitur sensitif disembunyikan.
function applyRemote() { $$('[data-local]').forEach(e => { e.hidden = true; }); }
function updateRemoteBanner(rm) {
  const b = $('#bannerOffline');
  if (!rm) { b.hidden = true; document.body.classList.remove('offline'); return; }
  b.hidden = rm.online;
  document.body.classList.toggle('offline', !rm.online);
  if (!rm.online) {
    const t = rm.lastSeen ? new Date(rm.lastSeen).toLocaleTimeString('id-ID', { hour: '2-digit', minute: '2-digit' }) : null;
    b.textContent = t ? `⚠ Mesin offline sejak ${t}. Kontrol dinonaktifkan sampai tersambung kembali.` : '⚠ Mesin belum pernah terhubung ke server.';
  }
}
async function start() {
  let r;
  try { r = await api('/api/poll?since=0'); } catch { return; }
  $('#app').hidden = false; $('#login').hidden = true;
  try { const info = await api('/api/info'); if (info && info.remote) applyRemote(); } catch { /* ESP32 lama tanpa /api/info */ }
  if (r.status) renderStatus(r.status);
  updateRemoteBanner(r.remote);
  addEntries(r.log.entries, false);
  if (r.log.entries.length >= 30) await fetchLogsAll();
  loadSettings(); loadMeta().then(() => { renderPalette(); return loadProses(); }); schedulePoll();
}
start();
