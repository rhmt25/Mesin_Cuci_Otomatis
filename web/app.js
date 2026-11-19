'use strict';
const $ = (s, r = document) => r.querySelector(s);
const $$ = (s, r = document) => [...r.querySelectorAll(s)];
let ws = null, wsOk = false, last = null, settings = null, lastState = 'idle';
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
  const r = await fetch(path, opt);
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
  if (t === 'settings' || t === 'water') loadSettings();
  if (t === 'wifi') loadWifi();
  if (t === 'relay') renderRelays();
  if (t === 'custom') { loadSettings().then(loadCustom); }
  if (t === 'history') loadHistory();
  if (t === 'firmware') loadFw();
  if (t === 'system') { renderSys(); loadDevices(); }
  if (t === 'log') renderLog();
  if (t === 'console') renderCon();
}

// --------------------------------------------------------------- login ----
function showLogin() { $('#login').hidden = false; $('#app').hidden = true; if (ws) { ws.close(); ws = null; } }
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
  if (last && last.state !== 'idle' && !confirm('Hentikan proses dan matikan semua relay?')) return;
  cmd('stop');
};

// ------------------------------------------------------------- status UI ----
function renderStatus(s) {
  last = s;
  $('#app').hidden = false; $('#login').hidden = true;
  const st = $('#chipState');
  st.textContent = { idle: 'Siap', running: 'Berjalan', paused: 'Dijeda' }[s.state];
  st.className = 'chip ' + (s.state === 'running' ? 'run' : s.state === 'paused' ? 'pause' : '');
  const w = $('#chipWifi');
  w.textContent = s.wifi.online ? `📶 ${s.wifi.ssid}` : '📴 offline';
  w.className = 'chip' + (s.wifi.online ? '' : ' off');
  $('#stage').textContent = s.state === 'idle' ? 'Siap' : s.stage;
  $('#detail').textContent = s.state === 'idle' ? (s.program || 'Pilih program di bawah') : s.detail;
  $('#stepLeft').textContent = s.state === 'idle' ? '-' : fmt(s.stepLeft);
  $('#totalLeft').textContent = s.state === 'idle' ? '-' : '±' + fmt(s.totalLeft);
  $('#water').textContent = s.water;
  const p = s.state === 'idle' ? 0 : s.progress;
  $('#ringFg').style.strokeDashoffset = 327 * (1 - p / 100);
  $('#ringText').textContent = s.state === 'idle' ? '—' : p + '%';
  $('#bannerResume').hidden = !s.resume.pending;
  $('#resumeInfo').textContent = s.resume.info;
  const bp = $('#bannerPause');
  bp.hidden = s.state !== 'paused';
  bp.textContent = '⏸ Dijeda: ' + (s.pause || 'oleh pengguna') + '. Tekan Lanjutkan untuk mencoba lagi.';
  $('#bannerFault').hidden = !s.fault;
  updateRelays(s);
  const busy = s.state !== 'idle';
  $$('.prog, .cpRun').forEach(b => { b.disabled = busy; });
  const pl = $('#progLock'); pl.hidden = !busy;
  pl.textContent = busy ? `Program baru hanya bisa dimulai saat mesin siap. Sekarang: ${s.program} (${s.stage}).` : '';
  lastState = s.state;
  if ($('#tab-system').classList.contains('on')) renderSys();
  if (s.otaBusy) { $('#fwBar').style.width = s.otaProgress + '%'; }
}

// ----------------------------------------------------- pengaturan dinamis ----
const NUM = [
  ['Soak (sesi)', 'set_soak_session', 'soak', 0, 1440], ['Wash (menit)', 'set_wash_time', 'wash', 0, 1440],
  ['Rinse (menit)', 'set_rinse_time', 'rinse', 0, 1440], ['Pewangi (cup, kelipatan 0,5)', 'set_jumlah_parfume', 'parfume', 0, 1440, 0.5],
  ['Spin (menit)', 'set_spin_time', 'spin', 0, 1440]];
const WNUM = [
  ['Coba ulang isi bila air habis (kali)', 'set_coba_ulang_isi', 'fillRetryCount', 0, 20],
  ['Tunggu antar coba ulang (menit)', 'set_tunggu_coba_ulang', 'fillRetryWait', 0, 240],
  ['Waktu buang, mode waktu (menit)', 'set_waktu_pembuangan_air', 'drainTime', 1, 1440],
  ['Batas sesi buang sebelum jeda', 'set_batas_sesi_pembuangan_air', 'drainSessionLimit', 1, 1440],
  ['Unjam valve buang (kali)', 'set_unjam', 'unjamCount', 0, 10],
  ['Tambahan buang setelah kosong (detik)', 'set_tambahan_buang', 'drainExtraSec', 0, 600],
  ['Maks. satu sesi buang (menit)', 'set_maks_buang', 'drainMaxMin', 1, 60]];
function numForms(host, defs) {
  host.innerHTML = defs.map(([label, c, key, lo, hi, step]) => `
    <div class="card numrow"><label>${esc(label)}<input type="number" data-key="${key}" min="${lo}" max="${hi}" step="${step || 1}"></label>
    <button class="btn" data-set="${c}" data-key="${key}">Simpan</button></div>`).join('');
  $$('[data-set]', host).forEach(b => b.onclick = () => cmd(b.dataset.set, $(`input[data-key=${b.dataset.key}]`, host).value));
}
async function loadSettings() {
  try { settings = await api('/api/settings'); } catch { return; }
  const s = settings;
  $$('input[data-key]').forEach(i => { if (document.activeElement !== i && s[i.dataset.key] !== undefined) i.value = s[i.dataset.key]; });
  $('#sPola').value = ['normal', 'gentle', 'hijab'][s.pola];
  $('#sNotif').value = ['lengkap', 'ringkas', 'senyap'][s.notif];
  $('#dH').value = s.dH; $('#dM').value = s.dM; $('#dS').value = s.dS;
  $('#fH').value = s.flH; $('#fM').value = s.flM; $('#fS').value = s.flS;
  $('#wLevel').value = s.fillLevel === 0 ? 'penuh' : s.fillLevel === 2 ? 'waktu' : String(s.fillPercent);
  $('#wTime').value = s.fillTime;
  $('#wDrain').value = s.drainMode === 0 ? 'sensor' : 'waktu';
  $('#calibNote').textContent = s.calib.nFill ? `Rata-rata isi penuh ${fmt(s.calib.fillSec)} (${s.calib.nFill} sampel). Level persen memakai data ini.` : 'Belum ada data kalibrasi: level persen akan memakai sensor sampai ada 1 pengisian penuh.';
  $('#inFinish').disabled = !s.timeSynced;
  $('#schedNote').textContent = s.timeSynced ? (s.finishAt >= 0 ? `Terjadwal selesai jam ${String(Math.floor(s.finishAt / 60)).padStart(2, '0')}:${String(s.finishAt % 60).padStart(2, '0')}` : 'Berlaku untuk proses berikutnya.') : 'Jam belum sinkron (butuh internet): hanya "mulai nanti".';
}
numForms($('#numSettings'), NUM); numForms($('#waterSettings'), WNUM);
$('#btnPola').onclick = () => cmd('pola', $('#sPola').value);
$('#btnNotif').onclick = () => cmd('notif', $('#sNotif').value);
$('#btnDelay3').onclick = () => cmd('set_delay', `${$('#dH').value || 0} ${$('#dM').value || 0} ${$('#dS').value || 0}`);
$('#btnLimit').onclick = () => cmd('set_batas_waktu_pengisian_air', `${$('#fH').value || 0} ${$('#fM').value || 0} ${$('#fS').value || 0}`);
$('#btnLevel').onclick = () => { const v = $('#wLevel').value; cmd('level_air', v === 'waktu' ? 'waktu ' + $('#wTime').value : v); };
$('#btnDrainMode').onclick = () => cmd('mode_buang', $('#wDrain').value);
$('#btnResetAll').onclick = () => confirm('Reset semua pengaturan ke default?') && cmd('reset_semua');
$('#btnCalib').onclick = () => confirm('Reset data kalibrasi isi/buang?') && cmd('kalibrasi_air');
$('#btnDelay').onclick = () => cmd('mulai_nanti', $('#inDelay').value || 0);
$('#btnFinish').onclick = () => { const v = $('#inFinish').value; v ? cmd('selesai_jam', v) : cmd('selesai_jam', 'batal'); };

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
  const seq = busy && s.program === 'Kalibrasi relay';
  const lock = $('#relayLock');
  if (s.fault) { lock.hidden = false; lock.textContent = '❌ Relay terkunci karena pelanggaran interlock. Restart mesin untuk membuka kunci.'; }
  else if (busy && !seq) {
    lock.hidden = false;
    lock.textContent = `🔒 Kontrol manual & uji dikunci: ada proses ${s.state === 'paused' ? 'yang dijeda' : 'berjalan'} (${s.program}, ${s.stage}). Hentikan dulu di Beranda.`;
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
  $('#wifiInfo').innerHTML = `<b>Hotspot:</b> ${esc(w.ap)} (192.168.4.1)<br><b>Jaringan:</b> ${w.online ? esc(w.ssid) + ' · ' + esc(w.ip) + ' · ' + w.rssi + ' dBm' : 'offline'}`;
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
const LV = ['DEBUG', 'INFO', 'STEP', 'WARN', 'ERROR', 'DONE'];
function lineHtml(e) { return `<div class="l-${e.l}">${clock(e.t, e.e)} ${LV[e.l].padEnd(5)} ${esc(e.m)}</div>`; }
function addEntries(list, live) {
  let alertEntry = null;
  for (const e of list) {
    if (e.s <= lastSeq) continue;
    lastSeq = e.s; logLines.push(e); conLines.push(e);
    if (live && e.l >= 3 && e.l <= 4) alertEntry = e;   // WARN / ERROR dari mesin
  }
  if (alertEntry) toast((alertEntry.l >= 4 ? '❌ ' : '⚠ ') + alertEntry.m);
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
  if (!ws || !wsOk) pollLogs();
};
async function pollLogs() {
  try { const r = await api('/api/logs?since=' + lastSeq + '&limit=100'); addEntries(r.entries); } catch { }
}

// -------------------------------------------------------- riwayat/fw/sistem ----
async function loadHistory() {
  const r = await api('/api/history');
  const st = r.stats;
  $('#statsBox').innerHTML = `<b>Total siklus selesai:</b> ${st.cycles}<br><b>Motor cuci menyala:</b> ${(st.motorSec / 3600).toFixed(1)} jam<br><b>Rata-rata isi penuh:</b> ${st.avgFillSec ? fmt(st.avgFillSec) : '-'}<br><b>Rata-rata buang:</b> ${st.avgDrainSec ? fmt(st.avgDrainSec) : '-'}`;
  $('#histList').innerHTML = r.history.map(h => `<div class="hist"><div><b>${esc(h.program)}</b><br><span class="muted">${h.start ? new Date(h.start * 1000).toLocaleString('id-ID') : '-'}</span></div><div style="text-align:right">${fmt(h.dur)}<br><span class="muted">${h.result}</span></div></div>`).join('') || '<p class="muted">Belum ada riwayat.</p>';
}
async function loadFw() {
  const i = await api('/api/update/info');
  $('#fwInfo').innerHTML = `<b>Versi aktif:</b> ${esc(i.version)} · ${esc(i.build)}<br><b>Partisi:</b> ${i.running} (cadangan: ${i.other})<br><b>Ukuran firmware:</b> ${(i.sketchSize / 1024).toFixed(0)} KB · ruang ${(i.freeSpace / 1024).toFixed(0)} KB<br><b>Versi sebelumnya:</b> ${i.canRollback ? esc(i.previousBuild || 'tersedia') : 'tidak ada'}`;
  $('#btnRollback').disabled = !i.canRollback;
}
$('#btnFlash').onclick = () => {
  const f = $('#fwFile').files[0];
  if (!f) return toast('Pilih file .bin dulu');
  if (last && last.state !== 'idle') return toast('Mesin sedang bekerja. Hentikan proses dulu.');
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
  $('#sysInfo').innerHTML = `<b>Firmware:</b> v${esc(s.fw)}<br><b>Uptime:</b> ${fmt(s.uptime)}<br><b>Heap bebas:</b> ${(s.heap / 1024).toFixed(0)} KB<br><b>Alasan reset terakhir:</b> ${esc(s.reset)}<br><b>WiFi:</b> ${s.wifi.online ? esc(s.wifi.ssid) + ' · ' + esc(s.wifi.ip) : 'offline'}<br><b>Hotspot:</b> ${esc(s.wifi.ap)} (${s.wifi.apClients} klien)<br><b>Jam:</b> ${esc(s.wifi.time)}<br><b>Notifikasi:</b> ${esc(s.notif)}`;
}
$('#btnPw').onclick = async () => { const r = await cmd('web_password', $('#newPw').value); if (r && r.ok) { $('#newPw').value = ''; toast('Password diganti. Silakan login ulang.'); setTimeout(showLogin, 800); } };
$('#btnRestart').onclick = () => confirm('Restart mesin?') && cmd('restart');

// -------------------------------------------------------------- Custom Proses ----
const POLA = [['3', 'Ikut Pengaturan'], ['0', 'Normal'], ['1', 'Gentle'], ['2', 'Hijab']];
const CI = {
  wash: { label: 'Cuci', icon: '🧼', hint: 'isi air → putar → buang', f: [['a', 'Menit putar', 15]], pola: 1, g: 'blok' },
  soakwash: { label: 'Soak + Cuci', icon: '🫧', hint: 'isi air → soak → putar → buang', f: [['a', 'Sesi soak', 2], ['b', 'Menit cuci', 15]], pola: 1, g: 'blok' },
  rinse: { label: 'Bilas', icon: '💧', hint: 'isi air → putar → buang', f: [['a', 'Menit putar', 10]], pola: 1, g: 'blok' },
  rinseperfume: { label: 'Bilas + Pewangi', icon: '🌸', hint: 'isi air → pewangi → putar → buang', f: [['a', 'Menit putar', 10]], pola: 1, cups: 1, g: 'blok' },
  fill: { label: 'Isi air', icon: '🚰', f: [], g: 'dasar' },
  agitate: { label: 'Putar', icon: '🔄', f: [['a', 'Menit', 5]], pola: 1, g: 'dasar' },
  soak: { label: 'Soak (rendam)', icon: '🛁', hint: '1 mnt putar + 4 mnt rendam per sesi', f: [['a', 'Sesi', 2]], pola: 1, g: 'dasar' },
  perfume: { label: 'Pewangi', icon: '🌼', f: [], cups: 1, g: 'dasar' },
  drain: { label: 'Buang air', icon: '🚿', f: [], g: 'dasar' },
  spin: { label: 'Spin', icon: '🌀', f: [['a', 'Menit', 10]], g: 'dasar' },
  wait: { label: 'Tunggu', icon: '⏳', f: [['a', 'Menit', 5]], g: 'dasar' },
};
const PRESETS = {
  normal: [['soakwash', 2, 15, 0], ['rinse', 10, 0, 0], ['rinseperfume', 10, 0, 0]],
  gentle: [['soakwash', 2, 12, 1], ['rinse', 8, 0, 1], ['rinseperfume', 8, 0, 1]],
  hijab: [['soakwash', 1, 10, 2], ['rinse', 8, 0, 2], ['rinseperfume', 8, 0, 2]],
  cepat: [['wash', 8, 0, 0], ['rinse', 5, 0, 0], ['rinseperfume', 5, 0, 0]],
};
let cpUid = 1, cpSlots = [];
const cp = { slot: 0, items: [] };
const newItem = (t, a, b, p) => { const m = CI[t]; return { uid: cpUid++, t, a: a ?? (m.f[0] ? m.f[0][2] : 0), b: b ?? (m.f[1] ? m.f[1][2] : 0), p: p ?? 3, c: 0 }; };
const stepsOf = it => ({ wash: 3, rinse: 3, soakwash: 4, rinseperfume: 4 }[it.t] || 1);
function estMin(it) {
  const c = settings && settings.calib ? settings.calib : {};
  const fillM = c.fillSec ? c.fillSec / 60 : 10, drM = (c.drainSec ? c.drainSec / 60 : 5) + 1;
  switch (it.t) {
    case 'wash': case 'rinse': return fillM + it.a + drM;
    case 'soakwash': return fillM + it.a * 5 + it.b + drM;
    case 'rinseperfume': return fillM + it.a + drM + 0.3;
    case 'fill': return fillM; case 'drain': return drM; case 'perfume': return 0.3;
    case 'agitate': case 'spin': case 'wait': return it.a; case 'soak': return it.a * 5;
  }
  return 0;
}
const fmtMin = m => m >= 60 ? `${Math.floor(m / 60)} j ${Math.round(m % 60)} mnt` : `${Math.round(m)} mnt`;
const itemText = it => { const m = CI[it.t]; const v = m.f.map(([k]) => it[k]); return m.f.length === 0 ? m.label : it.t === 'soakwash' ? `${m.label} ${v[0]}×/${v[1]}m` : it.t === 'soak' ? `${m.label} ${v[0]}×` : `${m.label} ${v[0]}m`; };
const cpTotal = items => ({ steps: items.reduce((n, i) => n + stepsOf(i), 0), min: items.reduce((n, i) => n + estMin(i), 0) });

function buildPalette() {
  for (const g of ['blok', 'dasar']) {
    $(g === 'blok' ? '#cpPalBlok' : '#cpPalDasar').innerHTML = Object.entries(CI).filter(([, m]) => m.g === g)
      .map(([t, m]) => `<button class="btn" data-add="${t}" title="${esc(m.hint || '')}">${m.icon} ${m.label}</button>`).join('');
  }
  $$('[data-add]').forEach(b => b.onclick = () => { cp.items.push(newItem(b.dataset.add)); renderStack(); const st = $('#cpStack'); st.lastElementChild && st.lastElementChild.scrollIntoView({ block: 'nearest' }); });
}
function rowHtml(it, i) {
  const m = CI[it.t];
  const fields = m.f.map(([k, lab]) => `<label>${lab}<input type="number" min="1" max="1440" inputmode="numeric" data-uid="${it.uid}" data-k="${k}" value="${it[k]}"></label>`).join('');
  const pola = m.pola ? `<label>Pola putar<select data-uid="${it.uid}" data-k="p">${POLA.map(([v, l]) => `<option value="${v}"${+v === it.p ? ' selected' : ''}>${l}</option>`).join('')}</select></label>` : '';
  const cups = m.cups ? `<label>Pewangi (cup)<input type="number" step="0.5" min="0" max="20" inputmode="decimal" data-uid="${it.uid}" data-k="c" value="${it.c ? it.c / 2 : ''}" placeholder="ikut Pengaturan"></label>` : '';
  return `<div class="crow" data-uid="${it.uid}"><span class="handle" title="Tahan &amp; geser untuk menukar urutan">☰</span>
    <div class="cbody"><div class="ctitle"><span class="num">${i + 1}</span> ${m.icon} <b>${m.label}</b> <span class="muted">${m.hint || ''}</span></div><div class="fields">${fields}${pola}${cups}</div></div>
    <div class="cacts"><button data-act="up" title="Naik">▲</button><button data-act="down" title="Turun">▼</button><button data-act="dup" title="Gandakan">⧉</button><button data-act="del" title="Hapus">🗑</button></div></div>`;
}
function renderStack() {
  $('#cpStack').innerHTML = cp.items.map(rowHtml).join('');
  $('#cpEmpty').hidden = cp.items.length > 0;
  updateSummary();
}
function renumber() { $$('#cpStack .crow').forEach((r, i) => { $('.num', r).textContent = i + 1; }); }
function updateSummary() {
  const t = cpTotal(cp.items);
  $('#cpSummary').textContent = cp.items.length ? `${cp.items.length} langkah (${t.steps} langkah mesin) · perkiraan ± ${fmtMin(t.min)}${t.steps > 99 ? ' · TERLALU PANJANG' : ''}` : '';
}
const findItem = uid => cp.items.find(x => x.uid === +uid);
$('#cpStack').addEventListener('input', e => {
  const el = e.target.closest('[data-k]'); if (!el) return;
  const it = findItem(el.dataset.uid); if (!it) return;
  const k = el.dataset.k;
  if (k === 'p') it.p = +el.value;
  else if (k === 'c') it.c = Math.max(0, Math.min(40, Math.round((parseFloat(el.value) || 0) * 2)));
  else it[k] = parseInt(el.value, 10) || 0;
  updateSummary();
});
$('#cpStack').addEventListener('click', e => {
  const b = e.target.closest('[data-act]'); if (!b) return;
  const uid = +b.closest('.crow').dataset.uid, i = cp.items.findIndex(x => x.uid === uid);
  if (b.dataset.act === 'del') cp.items.splice(i, 1);
  else if (b.dataset.act === 'up' && i > 0) [cp.items[i - 1], cp.items[i]] = [cp.items[i], cp.items[i - 1]];
  else if (b.dataset.act === 'down' && i < cp.items.length - 1) [cp.items[i + 1], cp.items[i]] = [cp.items[i], cp.items[i + 1]];
  else if (b.dataset.act === 'dup') cp.items.splice(i + 1, 0, { ...cp.items[i], uid: cpUid++ });
  renderStack();
});
// Geser untuk menukar urutan (pointer events: jalan di mouse & layar sentuh)
$('#cpStack').addEventListener('pointerdown', e => {
  const h = e.target.closest('.handle'); if (!h) return;
  const list = $('#cpStack'), row = h.closest('.crow');
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
    cp.items = ids.map(id => findItem(id));
    renumber(); updateSummary();
  };
  h.addEventListener('pointermove', move); h.addEventListener('pointerup', up); h.addEventListener('pointercancel', up);
});
$('#cpPreset').onchange = e => {
  const p = PRESETS[e.target.value]; if (!p) return;
  if (cp.items.length && !confirm('Ganti susunan sekarang dengan contoh ini?')) { e.target.value = ''; return; }
  cp.items = p.map(([t, a, b, pola]) => newItem(t, a, b, pola));
  if (!$('#cpName').value.trim()) $('#cpName').value = 'Salinan ' + CI_NAME(e.target.value);
  e.target.value = ''; renderStack();
};
const CI_NAME = k => ({ normal: 'Normal', gentle: 'Gentle', hijab: 'Hijab', cepat: 'Cepat' }[k]);
function cpClear(title) { cp.slot = 0; cp.items = []; $('#cpName').value = ''; $('#cpError').hidden = true; $('#cpTitle').textContent = title || 'Proses baru'; renderStack(); }
$('#cpReset').onclick = () => { if (!cp.items.length || confirm('Kosongkan editor?')) cpClear(); };
async function cpSave(asNew) {
  const err = $('#cpError'); err.hidden = true;
  const name = $('#cpName').value.trim();
  const fail = m => { err.textContent = m; err.hidden = false; err.scrollIntoView({ block: 'nearest' }); };
  if (!name) return fail('Nama proses wajib diisi.');
  if (!cp.items.length) return fail('Tambahkan minimal satu langkah.');
  for (let i = 0; i < cp.items.length; i++) {
    const it = cp.items[i], m = CI[it.t];
    for (const [k, lab] of m.f) if (!(it[k] >= 1 && it[k] <= 1440)) return fail(`Langkah ${i + 1} (${m.label}): "${lab}" harus 1-1440.`);
  }
  if (cpTotal(cp.items).steps > 99) return fail('Terlalu panjang. Kurangi langkah.');
  const body = { slot: asNew ? 0 : cp.slot, name, items: cp.items.map(({ t, a, b, p, c }) => ({ t, a, b, p, c })) };
  let r; try { r = await api('/api/custom', 'POST', body); } catch (e) { return; }
  if (!r.ok) return fail(r.msg);
  cp.slot = r.slot; toast('✅ ' + r.msg);
  $('#cpTitle').textContent = `Mengedit: ${name} (slot ${r.slot})`;
  loadCustom();
}
$('#cpSave').onclick = () => cpSave(false);
$('#cpSaveNew').onclick = () => cpSave(true);
function cpLoad(slotObj, asCopy) {
  cp.slot = asCopy ? 0 : slotObj.slot;
  cp.items = slotObj.items.map(i => ({ uid: cpUid++, t: i.t, a: i.a, b: i.b, p: i.p, c: i.c }));
  $('#cpName').value = asCopy ? slotObj.name + ' (salinan)' : slotObj.name;
  $('#cpTitle').textContent = asCopy ? 'Proses baru (salinan)' : `Mengedit: ${slotObj.name} (slot ${slotObj.slot})`;
  $('#cpError').hidden = true;
  renderStack(); $('#cpEditor').scrollIntoView({ behavior: 'smooth', block: 'start' });
}
async function loadCustom() {
  let r; try { r = await api('/api/custom'); } catch { return; }
  cpSlots = r.slots.filter(Boolean);
  const idle = !last || last.state === 'idle';
  $('#cpList').innerHTML = cpSlots.map(s => {
    const t = cpTotal(s.items.map(i => ({ ...i, t: i.t })));
    return `<div class="card cpcard"><div class="info"><b>${s.slot}. ${esc(s.name)}</b><br><span class="muted">${esc(s.items.map(itemText).join(' → '))}</span><br><span class="muted">${s.items.length} langkah · ± ${fmtMin(t.min)}</span></div>
    <div class="row"><button class="btn primary sm cpRun" data-run="${s.slot}" ${idle ? '' : 'disabled'}>▶ Jalankan</button><button class="btn sm" data-edit="${s.slot}">✎ Edit</button><button class="btn sm" data-copy="${s.slot}">⧉ Salin</button><button class="btn danger sm" data-del="${s.slot}">Hapus</button></div></div>`;
  }).join('') || '<p class="muted">Belum ada proses tersimpan.</p>';
  $$('#cpList [data-run]').forEach(b => b.onclick = () => cmd('custom', b.dataset.run));
  $$('#cpList [data-edit]').forEach(b => b.onclick = () => cpLoad(cpSlots.find(s => s.slot === +b.dataset.edit), false));
  $$('#cpList [data-copy]').forEach(b => b.onclick = () => cpLoad(cpSlots.find(s => s.slot === +b.dataset.copy), true));
  $$('#cpList [data-del]').forEach(b => b.onclick = async () => {
    const s = cpSlots.find(x => x.slot === +b.dataset.del);
    if (!confirm(`Hapus proses "${s.name}"?`)) return;
    const d = await api('/api/custom?slot=' + s.slot, 'DELETE'); toast(d.ok ? '🗑 ' + d.msg : '⚠ ' + d.msg);
    if (cp.slot === s.slot) cpClear(); loadCustom();
  });
  const hc = $('#homeCustom');
  hc.hidden = cpSlots.length === 0;
  $('#homeCustomGrid').innerHTML = cpSlots.map(s => `<button class="card prog cpRun" data-hrun="${s.slot}" ${idle ? '' : 'disabled'}><b>🗂 ${esc(s.name)}</b><small>${s.items.length} langkah · ± ${fmtMin(cpTotal(s.items).min)}</small></button>`).join('');
  $$('#homeCustomGrid [data-hrun]').forEach(b => b.onclick = () => cmd('custom', b.dataset.hrun));
}
buildPalette(); renderStack();

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
function connectWs() {
  if (ws) return;
  ws = new WebSocket((location.protocol === 'https:' ? 'wss://' : 'ws://') + location.host + '/ws');
  ws.onopen = () => { wsOk = true; };
  ws.onmessage = e => {
    const m = JSON.parse(e.data);
    if (m.type === 'status') renderStatus(m); else if (m.type === 'log') addEntries(m.entries, true);
  };
  ws.onclose = () => { wsOk = false; ws = null; setTimeout(() => { if (!$('#app').hidden) connectWs(); }, 2500); };
}
async function start() {
  try {
    const s = await api('/api/status'); renderStatus(s);
  } catch { return; }
  const logs = await api('/api/logs?since=0&limit=100').catch(() => ({ entries: [] }));
  addEntries(logs.entries);
  loadSettings().then(loadCustom); connectWs();
}
setInterval(async () => {   // cadangan bila WebSocket putus
  if ($('#app').hidden || wsOk) return;
  try { renderStatus(await api('/api/status')); pollLogs(); } catch { }
}, 3000);
start();
