'use strict';
const $ = (s, r = document) => r.querySelector(s);
const $$ = (s, r = document) => [...r.querySelectorAll(s)];
let last = null, settings = null, REMOTE = false;
let conPaused = false, conLines = [], logLines = [], lastSeq = 0, pollFails = 0;

// ================================================================ util ====
const ic = (n, c = '') => `<svg class="ic ${c}" aria-hidden="true"><use href="#i-${n}"/></svg>`;
const esc = s => String(s).replace(/[&<>"]/g, c => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;' }[c]));
const TOAST_IC = { ok: 'check', warn: 'alert', err: 'alert', info: 'info' };
function toast(msg, kind = 'info') {
  const t = $('#toast');
  t.className = kind;
  t.innerHTML = ic(TOAST_IC[kind] || 'info') + '<span></span>';
  t.lastChild.textContent = msg;
  void t.offsetWidth; t.classList.add('show');
  clearTimeout(toast.t); toast.t = setTimeout(() => t.classList.remove('show'), 3200);
}
const toastR = r => toast(r.msg || (r.ok ? 'Berhasil' : 'Gagal'), r.ok ? 'ok' : 'warn');
async function api(path, method = 'GET', body) {
  const opt = { method, headers: {} };
  if (body !== undefined) { opt.headers['Content-Type'] = 'application/json'; opt.body = JSON.stringify(body); }
  let r;
  // GET aman diulang bila koneksi tersendat sesaat; POST/DELETE tidak diulang (bisa terkirim dua kali)
  for (let i = 0; i < (method === 'GET' ? 3 : 1); i++) {
    try { r = await fetch(path, opt); break; } catch (e) { await new Promise(res => setTimeout(res, 500)); }
  }
  if (!r) throw new Error('Koneksi ke mesin terputus. Coba lagi sebentar.');
  if (r.status === 401 && path !== '/api/login') { showLogin(); throw new Error('login'); }
  return r.json();
}
async function cmd(name, args = '') {
  try {
    const r = await api('/api/cmd', 'POST', { cmd: name, args: String(args) });
    toastR(r);
    if (r.ok) setTimeout(loadSettings, 300);
    return r;
  } catch (e) { if (e.message !== 'login') toast('Gagal: ' + e.message, 'err'); }
}
const pad = n => String(n).padStart(2, '0');
const fmt = s => s >= 3600 ? `${Math.floor(s / 3600)}j ${Math.floor(s % 3600 / 60)}m` : s >= 60 ? `${Math.floor(s / 60)}m ${s % 60}d` : `${s}d`;
const fmtMin = s => s >= 3600 ? `${Math.floor(s / 3600)} j ${Math.round(s % 3600 / 60)} mnt` : `${Math.max(1, Math.round(s / 60))} mnt`;
const fmtClock = s => s >= 3600 ? `${Math.floor(s / 3600)}:${pad(Math.floor(s % 3600 / 60))}:${pad(s % 60)}` : `${pad(Math.floor(s / 60))}:${pad(s % 60)}`;
const fmtApprox = s => s >= 600 ? '±' + fmtMin(s) : fmtClock(s);   // detik hanya untuk < 10 menit; sisanya perkiraan
const hms = s => `${pad(Math.floor(s / 3600))}:${pad(Math.floor(s % 3600 / 60))}:${pad(s % 60)}`;
const clock = (ms, epoch) => epoch ? new Date(epoch * 1000).toTimeString().slice(0, 8) : `+${Math.floor(ms / 1000)}s`;
const fmtDate = e => e ? new Date(e * 1000).toLocaleString('id-ID', { dateStyle: 'medium', timeStyle: 'short' }) : '-';
const setCallout = (el, icon, html) => { el.innerHTML = ic(icon) + '<div>' + html + '</div>'; };
const guard = e => { if (e && e.message !== 'login') toast('Gagal: ' + e.message, 'err'); };

// ============================================================ tema ====
// Otomatis (ikuti sistem) / Terang / Gelap; pilihan disimpan di peramban.
function themeMode() { try { return localStorage.getItem('theme') || 'auto'; } catch { return 'auto'; } }
function setTheme(m) {
  try { m === 'auto' ? localStorage.removeItem('theme') : localStorage.setItem('theme', m); } catch { }
  const el = document.documentElement;
  if (m === 'auto') delete el.dataset.theme; else el.dataset.theme = m;
  $$('[data-theme-set]').forEach(b => b.setAttribute('aria-pressed', String(b.dataset.themeSet === m)));
  $('#btnTheme').innerHTML = ic(m === 'light' ? 'sun' : m === 'dark' ? 'moon' : 'monitor');
}
$('#btnTheme').onclick = () => setTheme({ auto: 'light', light: 'dark', dark: 'auto' }[themeMode()]);
$$('[data-theme-set]').forEach(b => b.onclick = () => setTheme(b.dataset.themeSet));
setTheme(themeMode());

// ======================================================== navigasi ====
const VIEWS = ['beranda', 'proses', 'manual', 'aktivitas', 'sistem'];
let curView = 'beranda', curSeg = 'log';
function show(v, seg) {
  if (!VIEWS.includes(v)) v = 'beranda';
  const changed = v !== curView;
  curView = v;
  $$('.view').forEach(x => x.classList.toggle('on', x.id === 'view-' + v));
  $$('.nav [data-view]').forEach(b => { if (b.dataset.view === v) b.setAttribute('aria-current', 'page'); else b.removeAttribute('aria-current'); });
  if (changed) { try { window.scrollTo({ top: 0 }); } catch { } }
  onView(v, seg);
}
function onView(v, seg) {
  if (v === 'beranda') renderHomeProses();
  if (v === 'proses') loadMeta().then(() => { loadSettings(); loadProses(); renderPalette(); });
  if (v === 'manual') { loadMeta().then(renderManual); renderRelays(); }
  if (v === 'aktivitas') actSeg(seg || curSeg);
  if (v === 'sistem') { loadSettings(); renderSys(); if (!REMOTE) { loadWifi(); loadDevices(); loadFw(); } }
}
$$('.nav [data-view]').forEach(b => b.onclick = () => { location.hash = '#' + b.dataset.view; show(b.dataset.view); });
window.addEventListener('hashchange', () => { const [v, sg] = location.hash.slice(1).split('/'); if (VIEWS.includes(v)) show(v, sg); });
function actSeg(seg) {
  if (seg === 'konsol' && REMOTE) seg = 'log';
  if (!['log', 'riwayat', 'konsol'].includes(seg)) seg = 'log';
  curSeg = seg;
  $$('#actSeg [data-seg]').forEach(b => b.classList.toggle('on', b.dataset.seg === seg));
  $$('.segpane').forEach(p => p.classList.toggle('on', p.id === 'seg-' + seg));
  if (seg === 'log') renderLog();
  if (seg === 'riwayat') loadHistory();
  if (seg === 'konsol') renderCon();
}
$$('#actSeg [data-seg]').forEach(b => b.onclick = () => actSeg(b.dataset.seg));
$('#sysToc').addEventListener('click', e => { const b = e.target.closest('[data-go]'); if (b) $('#' + b.dataset.go).scrollIntoView({ behavior: 'smooth', block: 'start' }); });

// ============================================================ login ====
function showLogin() { $('#login').hidden = false; $('#app').hidden = true; clearTimeout(pollTimer); }
$('#loginForm').onsubmit = async e => {
  e.preventDefault();
  const r = await api('/api/login', 'POST', { password: $('#pw').value, remember: $('#remember').checked }).catch(() => ({ ok: false, msg: 'Gagal terhubung' }));
  if (r.ok) { $('#pw').value = ''; $('#loginMsg').textContent = ''; start(); } else $('#loginMsg').textContent = r.msg || 'Gagal';
};
$('#btnLogout').onclick = async () => { await api('/api/logout', 'POST'); showLogin(); };
document.addEventListener('click', e => {
  const b = e.target.closest('[data-cmd]'); if (!b) return;
  cmd(b.dataset.cmd, b.dataset.args || '');
});
$('#btnStop').onclick = () => {
  if (last && last.state !== 'idle' && !confirm('Hentikan Proses dan matikan semua relay?')) return;
  cmd('stop');
};

// ======================================================= status UI ====
const STATE_TXT = { idle: 'Siap', running: 'Berjalan', paused: 'Dijeda', decide: 'Menunggu keputusan' };
const RELAY_UI = { kiri: ['rotate', 1], kanan: ['rotate', 1], isi: ['drop', 0], buang: ['drain', 0], spin: ['spin', 1], pewangi: ['flower', 0] };
const bars = r => r > -55 ? 4 : r > -65 ? 3 : r > -75 ? 2 : 1;
function renderChips(s) {
  const st = $('#chipState');
  const run = s.state === 'running', hold = s.state === 'paused' || s.state === 'decide';
  st.className = 'chip' + (run ? ' run dot' : hold ? ' warn' : '');
  st.innerHTML = (run ? '' : ic(hold ? (s.state === 'decide' ? 'alert' : 'pause') : 'check')) + esc(STATE_TXT[s.state] || s.state);
  const w = $('#chipWifi');
  if (pollFails >= 2) { w.className = 'chip warn'; w.innerHTML = ic('refresh') + 'Menyambung ulang…'; }
  else if (s.wifi.online) { w.className = 'chip'; w.innerHTML = ic('wifi') + '<span class="ell">' + esc(s.wifi.ssid) + '</span>'; }
  else { w.className = 'chip err'; w.innerHTML = ic('wifi-off') + 'offline'; }
}
function renderStatus(s) {
  last = s;
  $('#app').hidden = false; $('#login').hidden = true;
  const idle = s.state === 'idle', waitStart = s.state === 'running' && s.step === 0;
  renderChips(s);
  // ---- hero ----
  const hero = $('#hero');
  hero.classList.toggle('idle', idle); hero.classList.toggle('live', !idle);
  $('#stage').textContent = waitStart ? 'Menunggu mulai' : STATE_TXT[s.state] || s.state;
  $('#heroName').textContent = idle ? 'Mesin siap' : (s.proses || 'Proses');
  $('#detail').textContent = idle ? 'Pilih Proses di bawah untuk mulai mencuci.' : s.detail || '';
  $('#totalLeft').textContent = idle ? '-' : fmtApprox(s.totalLeft);
  $('#etaAt').textContent = !idle && s.totalLeft > 0 ? 'selesai sekitar ' + new Date(Date.now() + s.totalLeft * 1000).toTimeString().slice(0, 5) : '';
  $('#stepLine').textContent = idle ? 'Tidak ada Proses berjalan' : s.stage || '';
  $('#stepLeft').textContent = idle ? '-' : waitStart ? fmtApprox(s.stepLeft) : fmtClock(s.stepLeft);
  const p = idle ? 0 : s.progress;
  $('#ringFg').style.strokeDashoffset = 327 * (1 - p / 100);
  $('#ringText').textContent = idle ? '—' : p + '%';
  const pb = $('#btnPause');
  const paused = s.state === 'paused';
  pb.dataset.cmd = paused ? 'lanjutkan' : 'jeda';
  pb.innerHTML = ic(paused ? 'play' : 'pause') + `<span>${paused ? 'Lanjutkan' : 'Jeda'}</span>`;
  pb.disabled = idle || s.state === 'decide';
  $('#btnSkip').disabled = idle; $('#btnStop').disabled = idle;
  $('#btnJob').disabled = idle; $('#btnJob2').disabled = idle;
  // ---- callouts ----
  $('#bannerResume').hidden = !s.resume.pending;
  $('#resumeInfo').textContent = s.resume.info;
  $('#bannerFault').hidden = !s.fault;
  renderDecision(s);
  const nb = $('#navBadge'); nb.hidden = s.state !== 'decide';
  $('#navBadgeTxt').textContent = s.state === 'decide' ? 'Menunggu keputusan' : '';
  // ---- kartu status ----
  const wl = { penuh: 'Penuh', kosong: 'Tidak penuh', mengukur: 'Mengukur…' }[s.water] || s.water;
  $('#water').textContent = wl;
  $('#wdrop').setAttribute('class', 'ic wdrop' + (s.water === 'penuh' ? ' full' : s.water === 'mengukur' ? ' meas' : ''));
  const rl = s.relays || {};
  $('#relayMini').innerHTML = Object.keys(RELAY_UI).map(k => `<span class="rl${rl[k] ? ' on' : ''}${RELAY_UI[k][1] ? ' motor' : ''}">${ic(RELAY_UI[k][0])}${k}<span class="sr">${rl[k] ? 'menyala' : 'mati'}</span></span>`).join('');
  $('#connWifi').innerHTML = s.wifi.online ? `${esc(s.wifi.ssid)} <span class="bars" aria-label="sinyal ${bars(s.wifi.rssi)} dari 4">${[1, 2, 3, 4].map(i => `<i class="${i <= bars(s.wifi.rssi) ? 'on' : ''}"></i>`).join('')}</span>` : 'Offline';
  const rm = s.remote || {};
  $('#connSrv').textContent = !rm.configured ? 'Tidak diatur' : !rm.enabled ? 'Nonaktif' : rm.connected ? 'Terhubung' : 'Menyambung…';
  $('#connTime').textContent = s.wifi.time || '-';
  updateRelays(s);
  $$('.prog, .cpRun').forEach(b => { b.disabled = !idle; });
  const pl = $('#progLock'); pl.hidden = idle;
  pl.textContent = idle ? '' : `Proses baru hanya bisa dimulai saat mesin siap. Sekarang: ${s.proses} (${s.stage}).`;
  if (curView === 'sistem') renderSys();
  if (curView === 'manual') $('#manRun').disabled = !idle || s.fault;
  if (s.otaBusy) $('#fwBar').style.width = s.otaProgress + '%';
  const key = s.state + '|' + (s.slot || 0);
  if (renderStatus.key !== key) { renderStatus.key = key; if (prosesList) renderHomeProses(); }
  const jk = key + '|' + s.step + '|' + (s.manual ? 1 : 0);
  if (renderStatus.jk !== jk) {
    renderStatus.jk = jk;
    if (idle) { JOB = { active: false }; renderRail(); if (sheetOpen) renderJobSheet(); } else loadJob();
  }
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
  $('#decMsg').textContent = d.msg;
  if (renderDecision.key === key) return;
  renderDecision.key = key;
  $('#decTitle').textContent = `Menunggu keputusan · ${s.proses}`;
  $('#decBtns').innerHTML = d.opts.map((o, i) => `<button class="btn ${o === 'batal' ? 'dout' : o === 'lanjut' || o === 'ulang' || o === 'buang' ? 'primary' : ''}" data-dec="${o}">${esc(d.labels[i])}</button>`).join('');
  $$('#decBtns [data-dec]').forEach(b => b.onclick = async () => {
    const o = b.dataset.dec;
    if (o === 'lanjut' && LANJUT_WARN[d.code] && !confirm('Peringatan: ' + LANJUT_WARN[d.code] + '\n\nTetap lanjutkan?')) return;
    if (o === 'batal' && !confirm('Batalkan Proses ini dan matikan semua relay?')) return;
    try { toastR(await api('/api/decide', 'POST', { pilih: o })); pollOnce(); } catch (e) { guard(e); }
  });
}

// ===================================================== Rel Tahap ====
// Data /api/job dimuat saat Tahap/keadaan berubah (bukan tiap poll); sisa waktu tetap dari status yang lebih segar.
let JOB = null, jobBusy = false, jobRetry = null;
const TAHAP_ICON = { isi: 'drop', buang: 'drain', putar: 'rotate', soak: 'soak', rendam: 'moon', spin: 'spin', pewangi: 'flower', tunggu: 'timer', cek: 'search', tindakan: 'user', notif: 'bell', buang_manual: 'drain', uji: 'bolt' };
const tIcon = t => ic(TAHAP_ICON[t] || 'layers');
const tLabel = t => t === 'uji' ? 'Uji relay' : (typeMeta(t) || {}).label || t;
async function loadJob() {
  if (jobBusy) return;
  jobBusy = true;
  try {
    const r = await api('/api/job');
    if (r && r.ok === false) { clearTimeout(jobRetry); jobRetry = setTimeout(loadJob, 1500); } else JOB = r;   // 503: sibuk, coba lagi
  } catch { } finally { jobBusy = false; }
  renderRail(); if (sheetOpen) renderJobSheet();
}
// Posisi Tahap berjalan; -1 bila menunggu mulai
const jobCur = () => JOB.waiting ? -1 : JOB.idx;
function renderRail() {
  const host = $('#railMini');
  if (!JOB || !JOB.active) { host.innerHTML = '<p class="muted">Pilih Proses di bawah untuk memulai. Urutan Tahap tampil di sini saat berjalan.</p>'; return; }
  const cur = jobCur(), n = JOB.count;
  const from = Math.max(0, Math.min(Math.max(cur, 0) - 1, n - 5)), to = Math.min(n, from + 5);
  let h = '<ol class="rail">';
  for (let i = from; i < to; i++) {
    const t = JOB.tahap[i], c = i < cur ? 'done' : i === cur ? 'run' : '';
    h += `<li class="${c}"><span class="node"></span><span class="tl">${esc(tLabel(t.t))}</span><span class="st">${i < cur ? 'selesai' : i === cur ? 'berjalan' : 'antre'}</span></li>`;
  }
  h += '</ol>';
  if (from > 0 || to < n) h += `<p class="more">${from > 0 ? from + ' sebelumnya · ' : ''}${to < n ? n - to + ' berikutnya' : ''}</p>`;
  host.innerHTML = h;
}
const fmtNum = v => String(v).replace('.', ',');
function fieldVal(f, v) {
  if (f.k === 'tunggu' && v === 0) return 'Tanpa Jeda';
  if (f.kind === 'enum') { const i = f.opts.indexOf(v); return i >= 0 ? f.optLabels[i] : String(v); }
  if (f.kind === 'choice') return `${v}${f.unit ? ' ' + f.unit : ''}`;
  if (f.kind === 'cup') return fmtNum(v) + ' cup';
  if (f.kind === 'text') return String(v);
  if (f.fmt === 'hms') return hms(v);
  return `${v}${f.unit ? ' ' + f.unit : ''}`;
}
function tahapRows(t) {
  if (t.t === 'uji') return `<div><dt>Relay</dt><dd>${esc(t.relay)}</dd></div><div><dt>Lama</dt><dd>${t.detik} dtk</dd></div>`;
  const m = typeMeta(t.t); if (!m) return '';
  return m.fields.filter(f => applies(f, t) && t[f.k] !== undefined)
    .map(f => `<div class="${f.kind === 'text' ? 'wide' : ''}"><dt>${esc(f.label)}</dt><dd>${esc(fieldVal(f, t[f.k]))}</dd></div>`).join('');
}
function renderJobSheet() {
  const list = $('#jobList');
  if (!JOB || !JOB.active) {
    $('#jobSub').textContent = '';
    list.innerHTML = '<div class="empty"><p>Tidak ada Proses berjalan.</p></div>';
    return;
  }
  const cur = jobCur(), n = JOB.count, s = last || {};
  $('#jobTitle').textContent = JOB.name;
  const left = s.totalLeft || 0;
  $('#jobSub').textContent = JOB.waiting ? `Menunggu mulai · ${n} Tahap` : `Tahap ${Math.min(cur + 1, n)} dari ${n}` + (left ? ` · sisa ${fmtApprox(left)} · selesai ±${new Date(Date.now() + left * 1000).toTimeString().slice(0, 5)}` : '');
  const keep = list.scrollTop;
  let h = '<div class="tl-rail">';
  if (JOB.waiting) h += `<div class="tcard run wait"><div class="thead">${ic('clock')}<div class="tt"><b>Menunggu mulai</b><small>Proses mulai otomatis</small></div><span class="badge-st">${fmtApprox(s.stepLeft || JOB.waitLeft)}</span></div></div>`;
  JOB.tahap.forEach((t, i) => {
    const st = i < cur ? 'done' : i === cur ? 'run' : '';
    const label = i < cur ? 'Selesai' : i === cur ? 'Berjalan' : 'Antre';
    const live = i === cur ? `<div class="tlive"><span>${esc(s.detail || JOB.detail || '')}</span><b>sisa ${fmtClock(s.stepLeft || 0)}</b></div>` : '';
    h += `<div class="tcard ${st}" data-i="${i}"><div class="thead" role="button" tabindex="0" aria-expanded="${st !== 'done'}"><span class="tico">${tIcon(t.t)}</span>
      <div class="tt"><b>${i + 1}. ${esc(tLabel(t.t))}</b><small>±${fmtMin(t.est)}</small></div><span class="badge-st">${label}</span></div>
      <dl class="tfields">${tahapRows(t)}</dl>${live}</div>`;
  });
  list.innerHTML = h + '</div>';
  list.scrollTop = keep;
}
let sheetOpen = false, sheetOpener = null;
async function openSheet() {
  if (sheetOpen || !last || last.state === 'idle') return;
  sheetOpen = true; sheetOpener = document.activeElement;
  const sh = $('#jobSheet'); sh.hidden = false;
  await loadMeta();
  renderJobSheet();
  requestAnimationFrame(() => sh.classList.add('open'));
  $('#jobPanel').focus();
  loadJob();
  setTimeout(() => { const c = $('#jobList .tcard.run'); if (c) c.scrollIntoView({ block: 'center', behavior: 'smooth' }); }, 80);
}
function closeSheet() {
  if (!sheetOpen) return;
  sheetOpen = false;
  const sh = $('#jobSheet'); sh.classList.remove('open');
  setTimeout(() => { if (!sheetOpen) sh.hidden = true; }, 180);
  if (sheetOpener && sheetOpener.focus) sheetOpener.focus();
}
$('#btnJob').onclick = openSheet; $('#btnJob2').onclick = openSheet;
$('#jobClose').onclick = closeSheet;
$('#jobSheet').addEventListener('click', e => {
  if (e.target.closest('[data-close]')) return closeSheet();
  const h = e.target.closest('.thead'); if (h) { const c = h.closest('.tcard'); c.classList.toggle('open'); h.setAttribute('aria-expanded', String(c.classList.contains('open') || !c.classList.contains('done'))); }
});
$('#jobSheet').addEventListener('keydown', e => {
  if (e.key === 'Escape') return closeSheet();
  if (e.key === 'Tab') {   // fokus terkunci di dalam sheet
    const f = $$('button,[tabindex="0"],[href]', $('#jobPanel')).filter(x => !x.disabled);
    if (!f.length) return;
    const a = f[0], z = f[f.length - 1];
    if (e.shiftKey && document.activeElement === a) { e.preventDefault(); z.focus(); }
    else if (!e.shiftKey && document.activeElement === z) { e.preventDefault(); a.focus(); }
  }
  if ((e.key === 'Enter' || e.key === ' ') && e.target.classList.contains('thead')) { e.preventDefault(); e.target.click(); }
});

// ===================================================== pengaturan ====
async function loadSettings() {
  try { settings = await api('/api/settings'); } catch { return; }
  const s = settings, nm = ['lengkap', 'ringkas', 'senyap'][s.notif];
  $('#sNotif').value = nm;
  $$('#notifSeg [data-notif]').forEach(b => b.setAttribute('aria-pressed', String(b.dataset.notif === nm)));
  $('#calibNote').textContent = s.calib.nFill ? `Rata-rata isi penuh ${fmt(s.calib.fillSec)} (${s.calib.nFill} sampel). Mode isi "persen" memakai data ini.` : 'Belum ada data kalibrasi: mode isi "persen" memakai sensor sampai ada 1 pengisian penuh dengan mode sensor.';
  const sel = $('[data-sm=selesai]');
  sel.disabled = !s.timeSynced;
  if (!s.timeSynced && $('#startMode').value === 'selesai') setStartMode('now');
  $('#startNote').textContent = s.timeSynced ? '' : 'Jam belum sinkron (butuh internet): "Selesai pada jam" belum tersedia, gunakan tunda.';
}
$('#notifSeg').addEventListener('click', e => { const b = e.target.closest('[data-notif]'); if (b) { $('#sNotif').value = b.dataset.notif; cmd('notif', b.dataset.notif); } });
$('#btnResetAll').onclick = () => confirm('Reset pengaturan ke default?') && cmd('reset_semua');
$('#btnCalib').onclick = () => confirm('Reset data kalibrasi waktu isi?') && cmd('kalibrasi_air');

// ======================================================= Relay ====
const RELAYS = [['kiri', 'Motor putar kiri', 'GPIO25'], ['kanan', 'Motor putar kanan', 'GPIO18'], ['isi', 'Valve isi air', 'GPIO17'],
  ['buang', 'Motor valve buang', 'GPIO16'], ['spin', 'Motor spin', 'GPIO4'], ['pewangi', 'Pompa pewangi', 'GPIO13']];
function renderRelays() {
  const host = $('#relayCards');
  if (host.dataset.ready) return;
  host.dataset.ready = '1';
  host.innerHTML = RELAYS.map(([k, label, pin]) => `<div class="rcard"><div class="top2"><span class="led" id="led-${k}"></span><div><b>${label}</b><br><small>${k} · ${pin}</small></div></div>
    <div class="seg" role="group" aria-label="${label}"><button type="button" data-rl="${k}" data-on="1">ON</button><button type="button" data-rl="${k}" data-on="0">OFF</button></div>
    <span class="lockhint" id="hint-${k}"></span></div>`).join('');
  $$('[data-rl]', host).forEach(b => b.onclick = () => cmd('relay', `${b.dataset.rl} ${b.dataset.on === '1' ? 'on' : 'off'}`));
}
function updateRelays(s) {
  renderRelays();
  const on = s.relays || {}, busy = s.state !== 'idle', seq = busy && s.proses === 'Kalibrasi relay';
  const lock = $('#relayLock');
  if (s.fault) { lock.hidden = false; setCallout(lock, 'lock', 'Relay terkunci karena pelanggaran interlock. Restart mesin untuk membuka kunci.'); }
  else if (busy && !seq) { lock.hidden = false; setCallout(lock, 'lock', esc(`Kontrol manual dan uji dikunci: ada Proses ${s.state === 'running' ? 'berjalan' : 'yang menunggu/dijeda'} (${s.proses}, ${s.stage}). Hentikan dulu di Beranda.`)); }
  else lock.hidden = true;
  const conflict = {
    kiri: on.kanan ? 'Motor kanan sedang aktif, matikan dulu' : on.buang ? 'Valve buang sedang aktif, matikan dulu' : '',
    kanan: on.kiri ? 'Motor kiri sedang aktif, matikan dulu' : on.buang ? 'Valve buang sedang aktif, matikan dulu' : '',
    isi: on.buang ? 'Valve buang sedang aktif, matikan dulu' : '',
    buang: on.isi ? 'Valve isi sedang aktif, matikan dulu' : (on.kiri || on.kanan) ? 'Motor cuci sedang aktif, matikan dulu' : '',
  };
  RELAYS.forEach(([k]) => {
    const l = $('#led-' + k); if (l) l.classList.toggle('on', !!on[k]);
    const onBtn = $(`[data-rl="${k}"][data-on="1"]`), offBtn = $(`[data-rl="${k}"][data-on="0"]`);
    const c = conflict[k] || '', blocked = busy || s.fault;
    if (onBtn) { onBtn.disabled = blocked || !!c || !!on[k]; onBtn.title = c; onBtn.classList.toggle('on', !!on[k]); }
    if (offBtn) { offBtn.disabled = blocked || !on[k]; offBtn.classList.toggle('on', !on[k]); }
    const h = $('#hint-' + k);
    if (h) h.textContent = !blocked && c && !on[k] ? 'Terkunci: ' + c : '';
  });
  $('#btnSeq').disabled = busy || s.fault;
  const bp = $('#relayProgress');
  bp.hidden = !seq;
  if (seq) $('div', bp).textContent = `Uji berurutan: ${s.detail} (langkah ${s.step}/${s.steps})`;
}
$('#btnSeq').onclick = () => cmd('kalibrasi_relay', $('#seqSecs').value || 3);
$('#btnSeqStop').onclick = () => cmd('stop');
$('#btnAllOff').onclick = () => cmd('relay', 'semua off');

// ======================================================= WiFi ====
async function loadWifi() {
  let w; try { w = await api('/api/wifi'); } catch { return; }
  $('#wifiInfo').innerHTML = `<b>Hotspot:</b> ${w.apOn ? esc(w.ap) + ' aktif (192.168.4.1)' : 'nonaktif (menyala otomatis bila semua WiFi gagal)'}<br><b>Jaringan:</b> ${w.online ? esc(w.ssid) + ' · ' + esc(w.ip) + ' · ' + w.rssi + ' dBm' : 'offline'}`;
  $('#wifiList').innerHTML = w.networks.map(n => `<div class="wifirow"><div><b>${n.i}. ${esc(n.ssid)}</b> ${n.active ? '<span class="chip ok">aktif</span>' : ''}<br><small>${n.secured ? 'berkata sandi' : 'terbuka'}</small></div>
    <div class="row"><button class="icon-btn" data-up="${n.i}" aria-label="Naikkan">${ic('up')}</button><button class="icon-btn" data-dn="${n.i}" aria-label="Turunkan">${ic('down')}</button><button class="icon-btn" data-del="${n.i}" aria-label="Hapus ${esc(n.ssid)}">${ic('trash')}</button></div></div>`).join('') || '<p class="muted">Belum ada WiFi.</p>';
  $$('[data-del]').forEach(b => b.onclick = async () => { await cmd('wifi_hapus', b.dataset.del); loadWifi(); });
  $$('[data-up]').forEach(b => b.onclick = async () => { const i = +b.dataset.up; if (i > 1) { await cmd('wifi_urutan', `${i} ${i - 1}`); loadWifi(); } });
  $$('[data-dn]').forEach(b => b.onclick = async () => { const i = +b.dataset.dn; if (i < w.networks.length) { await cmd('wifi_urutan', `${i} ${i + 1}`); loadWifi(); } });
}
$('#wifiForm').onsubmit = async e => {
  e.preventDefault();
  try {
    const r = await api('/api/wifi', 'POST', { ssid: $('#wSsid').value, pass: $('#wPass').value });
    toastR(r);
    if (r.ok) { $('#wSsid').value = ''; $('#wPass').value = ''; setTimeout(loadWifi, 400); }
  } catch (x) { guard(x); }
};

// ================================================== Log & konsol ====
const LV = ['DEBUG', 'INFO', 'STEP', 'WARN', 'ERROR', 'DONE', 'NOTIF'];
const LVTXT = ['debug', 'info', 'tahap', 'warn', 'galat', 'selesai', 'notif'];
const lineHtml = e => `<div class="l-${e.l}">${clock(e.t, e.e)} ${(LV[e.l] || '?').padEnd(5)} ${esc(e.m)}</div>`;
const logRow = e => `<div class="lr l-${e.l}"><time>${clock(e.t, e.e)}</time><i>${LVTXT[e.l] || '?'}</i><span>${esc(e.m)}</span></div>`;
function addEntries(list, live) {
  let alertEntry = null;
  for (const e of list) {
    if (e.s <= lastSeq) continue;
    lastSeq = e.s; logLines.push(e); conLines.push(e);
    if (live && (e.l === 3 || e.l === 4 || e.l === 6)) alertEntry = e;   // WARN / ERROR / NOTIF dari mesin
  }
  if (alertEntry) toast(alertEntry.m, alertEntry.l === 4 ? 'err' : alertEntry.l === 6 ? 'info' : 'warn');
  if (logLines.length > 400) logLines.splice(0, logLines.length - 400);
  if (conLines.length > 600) conLines.splice(0, conLines.length - 600);
  if (curView === 'aktivitas') { if (curSeg === 'log') renderLog(); if (curSeg === 'konsol' && !conPaused) renderCon(); }
}
function renderLog() {
  const min = +$('#logLevel').value, box = $('#logBox');
  const stick = box.scrollTop + box.clientHeight >= box.scrollHeight - 30;
  const rows = logLines.filter(e => e.l >= min);
  box.innerHTML = rows.map(logRow).join('') || '<p class="muted" style="padding:12px">Belum ada catatan pada tingkat ini.</p>';
  if (stick) box.scrollTop = box.scrollHeight;
}
function renderCon() { const box = $('#conBox'); box.innerHTML = conLines.map(lineHtml).join(''); box.scrollTop = box.scrollHeight; }
$('#logLevel').onchange = renderLog;
$('#logFilters').addEventListener('click', e => {
  const b = e.target.closest('[data-lv]'); if (!b) return;
  $('#logLevel').value = b.dataset.lv;
  $$('#logFilters [data-lv]').forEach(x => x.setAttribute('aria-pressed', String(x === b)));
  renderLog();
});
$('#conPause').onclick = e => { conPaused = !conPaused; e.target.textContent = conPaused ? 'Lanjut' : 'Jeda'; if (!conPaused) renderCon(); };
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

// =========================================== riwayat / firmware / sistem ====
async function loadHistory() {
  let r; try { r = await api('/api/history'); } catch { return; }
  if (!r.stats) return;
  const st = r.stats;
  $('#statsBox').innerHTML = `<div class="stat"><b>${st.cycles}</b><small>Proses selesai</small></div><div class="stat"><b>${(st.motorSec / 3600).toFixed(1)} j</b><small>Motor cuci menyala</small></div><div class="stat"><b>${st.avgFillSec ? fmt(st.avgFillSec) : '-'}</b><small>Rata-rata isi penuh</small></div>`;
  const rc = { selesai: 'done', dihentikan: 'stop', error: 'err' };
  $('#histList').innerHTML = r.history.map(h => `<div class="hist"><div><b>${esc(h.proses)}</b><br><small>${h.start ? new Date(h.start * 1000).toLocaleString('id-ID') : '-'}</small></div><div style="text-align:right">${fmt(h.dur)}<br><span class="res ${rc[h.result] || ''}">${esc(h.result)}</span></div></div>`).join('') || '<p class="muted">Belum ada riwayat.</p>';
}
async function loadFw() {
  let i; try { i = await api('/api/update/info'); } catch { return; }
  if (!i.version) return;
  $('#fwInfo').innerHTML = `<div class="kv"><span>Versi aktif</span><b>${esc(i.version)} · ${esc(i.build)}</b></div><div class="kv"><span>Partisi</span><b>${i.running} (cadangan: ${i.other})</b></div><div class="kv"><span>Ukuran / ruang</span><b>${(i.sketchSize / 1024).toFixed(0)} KB / ${(i.freeSpace / 1024).toFixed(0)} KB</b></div><div class="kv"><span>Versi sebelumnya</span><b>${i.canRollback ? esc(i.previousBuild || 'tersedia') : 'tidak ada'}</b></div>`;
  $('#btnRollback').disabled = !i.canRollback;
}
$('#btnFlash').onclick = () => {
  const f = $('#fwFile').files[0];
  if (!f) return toast('Pilih file .bin dulu', 'warn');
  if (last && last.state !== 'idle') return toast('Mesin sedang bekerja. Hentikan Proses dulu.', 'warn');
  if (!confirm(`Flash ${f.name} (${(f.size / 1024).toFixed(0)} KB)? Mesin akan restart.`)) return;
  const fd = new FormData(); fd.append('firmware', f, f.name);
  const x = new XMLHttpRequest(); x.open('POST', '/api/update');
  x.upload.onprogress = e => { if (e.lengthComputable) { const p = Math.round(e.loaded / e.total * 100); $('#fwBar').style.width = p + '%'; $('#fwMsg').textContent = `Mengunggah ${p}%`; } };
  x.onload = () => { let r = {}; try { r = JSON.parse(x.responseText); } catch { } $('#fwMsg').textContent = r.msg || 'Selesai'; toast(r.msg || 'Selesai', r.ok ? 'ok' : 'warn'); if (r.ok) setTimeout(() => location.reload(), 9000); };
  x.onerror = () => { $('#fwMsg').textContent = 'Koneksi terputus (mesin mungkin sedang restart).'; };
  x.send(fd);
};
$('#btnRollback').onclick = async () => {
  if (!confirm('Kembali ke firmware sebelumnya dan restart?')) return;
  try { toastR(await api('/api/rollback', 'POST')); } catch (e) { guard(e); }
};
function renderSys() {
  if (!last) return;
  const s = last, kv = (k, v) => `<div class="kv"><span>${k}</span><b>${v}</b></div>`;
  $('#sysInfo').innerHTML = kv('Firmware', 'v' + esc(s.fw)) + kv('Uptime', fmt(s.uptime)) + kv('Heap bebas', (s.heap / 1024).toFixed(0) + ' KB') + kv('Alasan reset terakhir', esc(s.reset)) +
    kv('WiFi', s.wifi.online ? esc(s.wifi.ssid) + ' · ' + esc(s.wifi.ip) : 'offline') + kv('Hotspot', s.wifi.apOn ? esc(s.wifi.ap) + ' aktif (' + s.wifi.apClients + ' klien)' : 'nonaktif') +
    kv('Jam', esc(s.wifi.time)) + kv('Notifikasi', esc(s.notif)) + kv('Penyimpanan Proses', s.fs ? 'siap' : 'tidak tersedia');
  const rm = s.remote || {}, ri = $('#remoteInfo');
  const kvr = $('#remoteKv');
  if (kvr) kvr.innerHTML = kv('Dikonfigurasi', rm.configured ? 'Ya' : 'Belum') + kv('Status', rm.enabled ? 'Aktif' : 'Nonaktif') + kv('Server', rm.connected ? 'Terhubung' : 'Belum Terhubung');
  if (ri) ri.textContent = !rm.configured ? 'Belum dikonfigurasi (isi REMOTE_URL dan REMOTE_DEVICE_TOKEN di secrets.h).' : !rm.enabled ? 'Dinonaktifkan.' : rm.connected ? 'Terhubung ke server jarak jauh.' : 'Mencoba menyambung ke server…';
}
// Password: lokal dan jarak jauh berbeda, keduanya diatur dari sini (minimal 12 karakter, diketik dua kali)
function pwInputs(a, b) {
  const p1 = $('#' + a).value, p2 = $('#' + b).value;
  if (p1.length < 12) { toast('Password minimal 12 karakter', 'warn'); return null; }
  if (p1.length > 64) { toast('Password maksimal 64 karakter', 'warn'); return null; }
  if (p1 !== p2) { toast('Kedua password tidak sama', 'warn'); return null; }
  return p1;
}
$('#btnPwLocal').onclick = async () => {
  const pw = pwInputs('lpw1', 'lpw2'); if (!pw) return;
  if (!confirm('Ganti password web lokal? Semua perangkat yang diingat akan keluar.')) return;
  try {
    const r = await api('/api/password', 'POST', { which: 'local', pw });
    toastR(r);
    if (r.ok) { $('#lpw1').value = ''; $('#lpw2').value = ''; setTimeout(showLogin, 800); }
  } catch (e) { guard(e); }
};
$('#btnPwRemote').onclick = async () => {
  const pw = pwInputs('rpw1', 'rpw2'); if (!pw) return;
  if (!confirm('Ganti password jarak jauh? Semua sesi jarak jauh akan keluar. Catat password baru di tempat aman.')) return;
  const msg = $('#pwRemoteMsg');
  let r;
  try { r = await api('/api/password', 'POST', { which: 'remote', pw }); } catch (e) { return guard(e); }
  msg.textContent = r.msg;
  if (!r.ok) return;
  $('#rpw1').value = ''; $('#rpw2').value = '';
  for (let i = 0; i < 20; i++) {   // tunggu konfirmasi dari server (maks ±20 dtk)
    await new Promise(res => setTimeout(res, 1000));
    let s; try { s = await api('/api/status'); } catch { continue; }
    const st = s.remote && s.remote.pw;
    if (st === 'ok') { msg.textContent = s.remote.pwMsg; toast('Password jarak jauh diganti', 'ok'); return; }
    if (st === 'fail') { msg.textContent = s.remote.pwMsg; toast(s.remote.pwMsg, 'warn'); return; }
  }
  msg.textContent = 'Tidak ada konfirmasi dari server. Coba lagi.';
};
$('#btnRestart').onclick = () => confirm('Restart mesin?') && cmd('restart');
// ---- cadangkan & pulihkan (khusus lokal) ----
$('#btnBackup').onclick = () => { location.href = '/api/backup' + ($('#bkWifi').checked ? '?wifi=1' : ''); };
const readFileText = f => new Promise((res, rej) => { const fr = new FileReader(); fr.onload = () => res(fr.result); fr.onerror = () => rej(fr.error); fr.readAsText(f); });
$('#restoreFile').onchange = async e => {
  const f = e.target.files[0]; e.target.value = '';
  const msg = $('#restoreMsg');
  if (!f) return;
  if (f.size > 12288) { msg.textContent = 'Berkas terlalu besar (maks 12 KB).'; return; }
  let data;
  try { data = JSON.parse(await readFileText(f)); } catch { msg.textContent = 'Berkas bukan JSON yang valid.'; return; }
  const dry = await api('/api/restore?dry=1', 'POST', data).catch(() => null);
  if (!dry) { msg.textContent = 'Gagal memeriksa berkas.'; return; }
  if (!dry.ok) { msg.textContent = dry.msg; return; }
  if (!confirm(`Pulihkan dari cadangan?\n${dry.msg}\n\nPengaturan dan daftar WiFi saat ini akan diganti. Proses tidak ikut (ekspor/impor di menu Proses).`)) { msg.textContent = 'Dibatalkan.'; return; }
  const r = await api('/api/restore', 'POST', data).catch(() => null);
  msg.textContent = r && r.ok ? r.msg : (r ? r.msg : 'Gagal memulihkan.');
  if (r && r.ok) { loadSettings(); loadWifi(); toast('Pemulihan selesai', 'ok'); }
};

// ================================================= Tahap & Proses ====
// Katalog Tahap datang dari firmware (/api/tahap/meta): UI tidak menyimpan rentang/bawaan sendiri.
let META = [];
async function loadMeta() {
  if (META.length) return;
  try { const r = await api('/api/tahap/meta'); META = r.types || []; } catch { /* diulang saat menu dibuka lagi */ }
}
const typeMeta = t => META.find(x => x.t === t);
let uidSeq = 1;
function newItem(t, vals) {
  const m = typeMeta(t), v = {};
  m.fields.forEach(f => { v[f.k] = f.def; });
  return { uid: uidSeq++, t, v: { ...v, ...(vals || {}) } };
}
// f.when memakai nilai "mode": v bisa berupa nilai item (editor, {mode}) atau Tahap dari API (tampilan, juga punya mode)
const applies = (f, v) => !f.when || f.when.includes((v.v || v).mode);

const OPTIONAL = { 'isi.tunggu': 1 };
function fieldHtml(f, it) {
  const val = it.v[f.k], base = `data-uid="${it.uid}" data-k="${f.k}"`;
  let ctl, unit = '';
  if (f.kind === 'enum') ctl = `<select ${base}>${f.opts.map((o, i) => `<option value="${o}"${o === val ? ' selected' : ''}>${esc(f.optLabels[i])}</option>`).join('')}</select>`;
  else if (f.kind === 'choice') ctl = `<select ${base}>${f.opts.map(o => `<option value="${o}"${+o === +val ? ' selected' : ''}>${o}${f.unit || ''}</option>`).join('')}</select>`;
  else if (f.kind === 'cup') { ctl = `<input type="number" ${base} min="0.5" max="10" step="0.5" inputmode="decimal" value="${val}">`; unit = ' (cup)'; }
  else if (f.kind === 'text') ctl = `<input type="text" ${base} maxlength="${f.max}" value="${esc(val)}">`;
  else if (f.fmt === 'hms') {
    ctl = '<span class="tri">' + [['h', Math.floor(val / 3600), 23, 'jam'], ['m', Math.floor(val % 3600 / 60), 59, 'mnt'], ['s', val % 60, 59, 'dtk']]
      .map(([p, x, mx, cap]) => `<span><input type="number" ${base} data-hms="${p}" min="0" max="${mx}" inputmode="numeric" value="${x}" aria-label="${f.label} (${cap})"></span>`).join('') + '</span>';
    unit = ' (J : M : D)';
  } else { ctl = `<input type="number" ${base} min="${f.min}" max="${f.max}" inputmode="numeric" value="${val}">`; unit = f.unit ? ` (${f.unit})` : ''; }
  const cap = `<span>${esc(f.label)}<span class="u">${unit}</span></span>`;
  // Variabel opsional (nilai 0 = mati): saklar di baris label; kotak tetap ada agar sejajar dengan tetangganya
  if (OPTIONAL[it.t + '.' + f.k]) {
    const on = +val > 0;
    ctl = ctl.replace('<input ', `<input ${on ? '' : 'disabled '}`);
    return `<label data-f="${f.k}"${applies(f, it) ? '' : ' hidden'}><span class="optlab">${cap}<span class="sw"><input type="checkbox" data-opt="${it.uid}" data-k2="${f.k}"${on ? ' checked' : ''} aria-label="Gunakan ${esc(f.label)}"></span></span>${ctl}</label>`;
  }
  return `<label data-f="${f.k}"${applies(f, it) ? '' : ' hidden'}>${cap}${ctl}</label>`;
}
const fieldsHtml = it => typeMeta(it.t).fields.map(f => fieldHtml(f, it)).join('');

// Mengambil nilai dari kontrol ke item. Mengembalikan true bila event ini milik form Tahap.
function handleField(e, items) {
  const sw = e.target.closest('[data-opt]');
  if (sw) {   // saklar variabel opsional: mati = 0, nyala = nilai bawaan
    const it = items.find(x => x.uid === +sw.dataset.opt); if (!it) return false;
    const f = typeMeta(it.t).fields.find(x => x.k === sw.dataset.k2), inp = $('[data-k]', sw.closest('label'));
    it.v[f.k] = sw.checked ? f.def : 0; inp.value = it.v[f.k]; inp.disabled = !sw.checked;
    return true;
  }
  const el = e.target.closest('[data-k]'); if (!el) return false;
  const it = items.find(x => x.uid === +el.dataset.uid); if (!it) return false;
  const f = typeMeta(it.t).fields.find(x => x.k === el.dataset.k);
  if (f.kind === 'enum' || f.kind === 'text') it.v[f.k] = el.value;
  else if (f.kind === 'choice' || f.kind === 'cup') it.v[f.k] = parseFloat(el.value);
  else if (el.dataset.hms) {
    const [h, m, s] = $$('[data-hms]', el.closest('label')).map(i => parseInt(i.value, 10) || 0);
    it.v[f.k] = h * 3600 + m * 60 + s;
  } else it.v[f.k] = parseInt(el.value, 10);
  if (OPTIONAL[it.t + '.' + f.k] && el.value !== '' && it.v[f.k] === 0) {   // nilai 0 -> saklar otomatis mati
    const sw2 = $('[data-opt]', el.closest('label')); if (sw2) { sw2.checked = false; it.v[f.k] = 0; el.disabled = true; }
  }
  const body = el.closest('.fields');
  typeMeta(it.t).fields.forEach(g => { const lab = $(`[data-f="${g.k}"]`, body); if (lab) lab.hidden = !applies(g, it); });
  const row = el.closest('.crow'); if (row) { row.classList.remove('bad'); const fe = $('.ferr', row); if (fe) fe.remove(); }
  return true;
}
// Pesan galat bahasa Indonesia, atau '' bila valid (firmware tetap memvalidasi ulang)
function validateItem(it) {
  const m = typeMeta(it.t);
  for (const f of m.fields) {
    if (!applies(f, it)) continue;
    const v = it.v[f.k], pre = `${m.label}: ${f.label} harus `;
    if (f.kind === 'text') { if (!String(v).trim() || String(v).length > f.max) return pre + `diisi (1-${f.max} karakter)`; }
    else if (f.kind === 'enum') { if (!f.opts.includes(v)) return pre + 'salah satu pilihan'; }
    else if (f.kind === 'choice') { if (!f.opts.includes(+v)) return pre + 'salah satu dari ' + f.opts.join('/'); }
    else if (f.kind === 'cup') { if (!(v >= 0.5 && v <= 10 && Math.abs(v * 2 - Math.round(v * 2)) < 1e-6)) return pre + '0,5 - 10 cup (kelipatan 0,5)'; }
    else if (!(Number.isInteger(v) && v >= f.min && v <= f.max)) return pre + (f.fmt === 'hms' ? `${hms(f.min)} - ${hms(f.max)}` : `${f.min} - ${f.max}${f.unit ? ' ' + f.unit : ''}`);
  }
  return '';
}
const itemToApi = it => { const o = { t: it.t }; typeMeta(it.t).fields.forEach(f => { if (applies(f, it)) o[f.k] = it.v[f.k]; }); return o; };
const itemFromApi = o => newItem(o.t, Object.fromEntries(Object.entries(o).filter(([k]) => k !== 't')));

// ======================================================= Manual ====
const man = { item: null };
function renderManual() {
  if (!META.length) { $('#manForm').innerHTML = '<p class="muted">Memuat daftar Tahap…</p>'; return; }
  const grid = $('#manGrid'), cur = $('#manType').value;
  grid.innerHTML = META.map(m => `<button type="button" data-mt="${m.t}" aria-pressed="${m.t === cur}">${tIcon(m.t)}<span>${esc(m.label)}</span>${m.manual ? '<span class="dar">hanya Manual</span>' : ''}</button>`).join('');
  if (!man.item || man.item.t !== cur) man.item = newItem(cur);
  $('#manForm').innerHTML = `<div class="fields">${fieldsHtml(man.item)}</div>`;
  $('#manHint').hidden = cur !== 'buang_manual';
  $('#manRun').disabled = !(last && last.state === 'idle') || !!(last && last.fault);
}
$('#manGrid').addEventListener('click', e => { const b = e.target.closest('[data-mt]'); if (b) { $('#manType').value = b.dataset.mt; man.item = null; renderManual(); } });
$('#manType').onchange = () => { man.item = null; renderManual(); };
$('#manForm').addEventListener('input', e => { handleField(e, [man.item]); });
$('#manForm').addEventListener('change', e => { handleField(e, [man.item]); });
$('#manRun').onclick = async () => {
  if (!man.item) return;
  const err = validateItem(man.item);
  if (err) return toast(err, 'warn');
  if (man.item.t === 'buang_manual' && !confirm('Buang manual menarik valve selama waktu yang diatur TANPA membaca sensor. Pastikan Anda memantau. Lanjut?')) return;
  try { toastR(await api('/api/run', 'POST', { manual: itemToApi(man.item) })); pollOnce(); } catch (e) { guard(e); }
};

// ======================================================= Proses ====
let prosesList = null;
const ed = { slot: 0, items: [], locked: false, warnings: [], est: 0 };
const setEdMode = m => { $('#view-proses').dataset.mode = m; if (m === 'edit') { try { window.scrollTo({ top: 0 }); } catch { } } };
function renderPalette() {
  const host = $('#pcPalette');
  if (!META.length || host.dataset.ready) return;
  host.dataset.ready = '1';
  host.innerHTML = META.filter(m => !m.manual).map(m => `<button class="btn sm" type="button" data-add="${m.t}">${tIcon(m.t)}${esc(m.label)}</button>`).join('');
  $$('[data-add]', host).forEach(b => b.onclick = () => {
    if (ed.locked) return;
    if (ed.items.length >= (prosesList ? prosesList.maxTahap : 40)) return toast('Maksimal 40 Tahap per Proses', 'warn');
    ed.items.push(newItem(b.dataset.add)); renderStack(); scheduleDry();
    const st = $('#pcStack'); st.lastElementChild && st.lastElementChild.scrollIntoView({ block: 'nearest' });
  });
}
function rowHtml(it, i) {
  const m = typeMeta(it.t);
  return `<div class="crow" data-uid="${it.uid}"><div class="crow-head"><span class="handle" title="Tahan dan geser untuk menukar urutan" aria-label="Geser">${ic('grip')}</span>
    <span class="num">${i + 1}</span><span class="tico">${tIcon(it.t)}</span><b>${esc(m.label)}</b>
    <div class="cacts"><button type="button" data-act="up" title="Naik" aria-label="Naik">${ic('up', 'sm')}</button><button type="button" data-act="down" title="Turun" aria-label="Turun">${ic('down', 'sm')}</button><button type="button" data-act="dup" title="Gandakan" aria-label="Gandakan">${ic('copy', 'sm')}</button><button type="button" data-act="del" title="Hapus" aria-label="Hapus">${ic('trash', 'sm')}</button></div></div>
    <div class="fields">${fieldsHtml(it)}</div></div>`;
}
function renderStack() {
  $('#pcStack').innerHTML = ed.items.map(rowHtml).join('');
  $('#pcEmpty').hidden = ed.items.length > 0;
  renderEditorState();
}
const renumber = () => $$('#pcStack .crow').forEach((r, i) => { $('.num', r).textContent = i + 1; });
function renderEditorState() {
  const lk = $('#pcLock');
  lk.hidden = !ed.locked;
  if (ed.locked) setCallout(lk, 'lock', 'Proses ini sedang dipakai (berjalan, dijeda, menunggu keputusan, atau pemulihan tertunda). Tekan Stop di Beranda dulu untuk mengubah atau menghapusnya.');
  ['pcSave', 'pcName'].forEach(id => { $('#' + id).disabled = ed.locked; });
  $$('#pcStack input, #pcStack select, #pcStack button, #pcPalette button').forEach(x => { x.disabled = ed.locked; });
  $('#pcSummary').textContent = ed.items.length ? `${ed.items.length} Tahap${ed.est ? ' · perkiraan ± ' + fmtMin(ed.est) : ''}` : 'Belum ada Tahap';
  const w = $('#pcWarn');
  w.hidden = !ed.warnings.length;
  if (ed.warnings.length) setCallout(w, 'alert', '<b>Peringatan</b> (boleh disimpan setelah konfirmasi):<ul>' + ed.warnings.map(x => `<li>${esc(x)}</li>`).join('') + '</ul>');
}
const bodyOf = () => ({ name: $('#pcName').value.trim(), tahap: ed.items.map(itemToApi) });
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
  else if (b.dataset.act === 'dup') { if (ed.items.length >= 40) return toast('Maksimal 40 Tahap per Proses', 'warn'); ed.items.splice(i + 1, 0, { uid: uidSeq++, t: ed.items[i].t, v: { ...ed.items[i].v } }); }
  renderStack(); scheduleDry();
});
// Geser bebas untuk menukar urutan: seret dari mana saja pada kartu Tahap (kecuali kotak isian/tombol), tanpa menahan.
// Kartu yang diseret mengambang; penanda (placeholder) menandai posisi jatuh, dihitung dari kartu lain yang tidak ikut bergeser.
let dragSt = null;
const stackEl = $('#pcStack');
function cancelDrag(commit) {
  if (!dragSt) return;
  const d = dragSt; dragSt = null;
  clearTimeout(d.timer);
  document.removeEventListener('pointermove', dragMove); document.removeEventListener('pointerup', dragUp); document.removeEventListener('pointercancel', dragCancel);
  if (!d.on) return;
  d.row.style.cssText = ''; d.row.classList.remove('drag'); document.body.classList.remove('dragging');
  if (commit) {
    stackEl.insertBefore(d.row, d.ph);
    ed.items = [...stackEl.children].filter(r => r.classList.contains('crow')).map(r => ed.items.find(x => x.uid === +r.dataset.uid));
    renumber(); scheduleDry();
  }
  d.ph.remove();
}
function beginDrag() {
  const d = dragSt; if (!d) return;
  d.on = true;
  const r = d.row.getBoundingClientRect();
  d.ph = document.createElement('div'); d.ph.className = 'crow-ph'; d.ph.style.height = r.height + 'px';
  d.row.after(d.ph);
  d.off = d.y - r.top;
  d.row.classList.add('drag');
  d.row.style.cssText = `position:fixed;left:${r.left}px;top:${r.top}px;width:${r.width}px;z-index:40;pointer-events:none`;
  document.body.classList.add('dragging');
}
function dragMove(ev) {
  const d = dragSt; if (!d) return;
  if (!d.on) { if (Math.hypot((ev.clientX || 0) - d.x, ev.clientY - d.y) > 4) beginDrag(); else return; }   // seperti menyeret berkas: mulai setelah bergerak sedikit
  ev.preventDefault && ev.preventDefault();
  d.row.style.top = (ev.clientY - d.off) + 'px';
  const others = [...stackEl.children].filter(r => r !== d.row && r !== d.ph);
  const target = others.find(r => { const b = r.getBoundingClientRect(); return ev.clientY < b.top + b.height / 2; });
  if (target) { if (d.ph.nextElementSibling !== target) stackEl.insertBefore(d.ph, target); } else if (stackEl.lastElementChild !== d.ph) stackEl.appendChild(d.ph);
  if (ev.clientY < 70) window.scrollBy(0, -14); else if (ev.clientY > innerHeight - 70) window.scrollBy(0, 14);
}
const dragUp = () => cancelDrag(true), dragCancel = () => cancelDrag(false);
stackEl.addEventListener('pointerdown', e => {
  if (ed.locked || (e.button && e.button !== 0)) return;
  const row = e.target.closest('.crow'); if (!row) return;
  const onHandle = !!e.target.closest('.handle');
  if (!onHandle && e.target.closest('input,select,button,textarea,a,.sw')) return;
  cancelDrag(false);
  dragSt = { row, x: e.clientX || 0, y: e.clientY || 0, on: false };
  document.addEventListener('pointermove', dragMove); document.addEventListener('pointerup', dragUp); document.addEventListener('pointercancel', dragCancel);
  if (onHandle) { e.preventDefault(); beginDrag(); }
});
document.addEventListener('touchmove', e => { if (dragSt && dragSt.on) e.preventDefault(); }, { passive: false });
stackEl.addEventListener('contextmenu', e => { if (dragSt) e.preventDefault(); });
function edClear(title) {
  ed.slot = 0; ed.items = []; ed.locked = false; ed.warnings = []; ed.est = 0;
  $('#pcName').value = ''; $('#pcError').hidden = true; $('#pcTitle').textContent = title || 'Proses baru';
  renderStack();
}
$('#pcReset').onclick = () => { if (!ed.items.length || confirm('Kosongkan editor?')) edClear(); };
$('#pcBack').onclick = () => setEdMode('list');
async function pcSave(asNew) {
  const err = $('#pcError'); err.hidden = true;
  const fail = (m, idx) => {
    setCallout(err, 'alert', esc(m)); err.hidden = false;
    $$('#pcStack .crow').forEach(r => { r.classList.remove('bad'); const fe = $('.ferr', r); if (fe) fe.remove(); });
    const row = idx >= 0 ? $$('#pcStack .crow')[idx] : null;
    if (row) { row.classList.add('bad'); row.insertAdjacentHTML('beforeend', `<p class="ferr">${esc(m.replace(/^Tahap \d+: /, ''))}</p>`); row.scrollIntoView({ block: 'center' }); } else err.scrollIntoView({ block: 'nearest' });
  };
  const le = localError();
  if (le) { const mm = /^Tahap (\d+):/.exec(le); return fail(le, mm ? +mm[1] - 1 : -1); }
  const slot = asNew ? 0 : ed.slot;
  let dry;
  try { dry = await api('/api/proses', 'POST', { ...bodyOf(), slot, dry: true }); } catch (e) { return guard(e); }
  if (!dry.ok) return fail(dry.msg, -1);
  if (dry.warnings && dry.warnings.length && !confirm('Peringatan:\n- ' + dry.warnings.join('\n- ') + '\n\nTetap simpan?')) return;
  let r; try { r = await api('/api/proses', 'POST', { ...bodyOf(), slot }); } catch (e) { return guard(e); }
  if (!r.ok) return fail(r.msg, -1);
  ed.slot = r.slot; toast(r.msg, 'ok');
  $('#pcTitle').textContent = `Mengedit: ${$('#pcName').value.trim()} (slot ${r.slot})`;
  loadProses();
}
$('#pcSave').onclick = () => pcSave(false);

async function pcLoad(slot, asCopy) {
  let r; try { r = await api('/api/proses?slot=' + slot); } catch { return; }
  if (!r.proses) return toast(r.msg || 'Proses tidak bisa dimuat', 'warn');
  await loadMeta();
  ed.slot = asCopy ? 0 : slot;
  ed.locked = asCopy ? false : !!r.locked;
  ed.items = r.proses.tahap.map(itemFromApi);
  ed.warnings = r.warnings || []; ed.est = r.est || 0;
  $('#pcName').value = asCopy ? r.proses.name + ' (salinan)' : r.proses.name;
  $('#pcTitle').textContent = asCopy ? 'Proses baru (salinan)' : `Mengedit: ${r.proses.name} (slot ${slot})`;
  $('#pcError').hidden = true;
  renderStack(); setEdMode('edit');
}
async function pcExport(slot) {
  let r; try { r = await api('/api/proses/export?slot=' + slot); } catch { return; }
  if (!r.tahap) return toast(r.msg || 'Gagal mengekspor', 'warn');
  const a = document.createElement('a');
  a.href = URL.createObjectURL(new Blob([JSON.stringify(r, null, 1)], { type: 'application/json' }));
  a.download = 'proses-' + String(r.name).replace(/[^\w\-]+/g, '_') + '.json'; a.click();
}
$('#pcImportFile').onchange = async e => {
  const f = e.target.files[0]; e.target.value = '';
  if (!f) return;
  if (f.size > 8192) return toast('Berkas terlalu besar (maks 8 KB)', 'warn');
  let data; try { data = JSON.parse(await readFileText(f)); } catch { return toast('Berkas bukan JSON yang valid', 'warn'); }
  const empty = prosesList ? prosesList.slots.findIndex(s => !s) + 1 : 0;
  if (!empty) return toast('Penyimpanan penuh. Hapus satu Proses dulu.', 'warn');
  const r = await api('/api/proses/import?slot=' + empty, 'POST', data).catch(() => null);
  if (r && r.ok) toast('Diimpor ke slot ' + r.slot, 'ok'); else toast(r ? r.msg : 'Gagal mengimpor', 'warn');
  loadProses();
};
async function pcDelete(slot, name) {
  if (!confirm(`Hapus Proses "${name}"?`)) return;
  const d = await api('/api/proses?slot=' + slot, 'DELETE').catch(() => null);
  if (!d) return;
  toast(d.msg, d.ok ? 'ok' : 'warn');
  if (d.ok && ed.slot === slot) { edClear(); setEdMode('list'); }
  loadProses();
}
const trailHtml = (tr, n) => `<div class="trail" aria-hidden="true">${(tr || []).map(t => `<span>${tIcon(t)}</span>`).join('')}${n > (tr || []).length ? `<em>+${n - tr.length}</em>` : ''}</div>`;
async function loadProses() {
  let r; try { r = await api('/api/proses'); } catch { return; }
  if (!r.slots) return;
  prosesList = r;
  const slots = r.slots.map((s, i) => s ? { ...s, slot: i + 1 } : null).filter(Boolean);
  $('#pcList').innerHTML = slots.map(s => s.bad
    ? `<div class="pcard cpcard"><b class="nm">${s.slot}. Berkas rusak</b><div class="pcacts"><button class="btn dout sm" data-pdel="${s.slot}" data-name="berkas rusak">Hapus</button></div></div>`
    : `<div class="pcard cpcard">${trailHtml(s.trail, s.count)}<div><div class="nm">${esc(s.name)}</div>
      <div class="meta"><span>${s.count} Tahap</span><span>± ${fmtMin(s.est)}</span>${s.locked ? `<span class="chip run">${ic('lock')}dipakai</span>` : ''}</div></div>
      <div class="pcacts"><button class="btn sm" data-pedit="${s.slot}">${ic(s.locked ? 'search' : 'edit', 'sm')}${s.locked ? 'Lihat' : 'Edit'}</button><button class="btn sm sq" data-pcopy="${s.slot}" aria-label="Salin ${esc(s.name)}" title="Salin">${ic('copy', 'sm')}</button><button class="btn sm sq" data-pexp="${s.slot}" aria-label="Ekspor ${esc(s.name)}" title="Ekspor">${ic('download', 'sm')}</button><button class="btn dout sm sq" data-pdel="${s.slot}" data-name="${esc(s.name)}" aria-label="Hapus ${esc(s.name)}" title="Hapus" ${s.locked ? 'disabled' : ''}>${ic('trash', 'sm')}</button></div></div>`
  ).join('') + (slots.length < r.max ? `<button class="pcard new" id="pcNew" type="button">${ic('plus', 'lg')}Proses baru</button>` : '');
  $('#pcCount').textContent = `${slots.length}/${r.max}`;
  $$('#pcList [data-pedit]').forEach(b => b.onclick = () => pcLoad(+b.dataset.pedit, false));
  $$('#pcList [data-pcopy]').forEach(b => b.onclick = () => pcLoad(+b.dataset.pcopy, true));
  $$('#pcList [data-pexp]').forEach(b => b.onclick = () => pcExport(+b.dataset.pexp));
  $$('#pcList [data-pdel]').forEach(b => b.onclick = () => pcDelete(+b.dataset.pdel, b.dataset.name));
  const nw = $('#pcNew'); if (nw) nw.onclick = () => { edClear(); setEdMode('edit'); };
  renderHomeProses();
  if (ed.slot) { const cur = slots.find(s => s.slot === ed.slot); if (cur && ed.locked !== !!cur.locked) { ed.locked = !!cur.locked; renderEditorState(); } }
  if (!loadProses.hist) { loadProses.hist = true; lastUsedLoad(); }
}

// ================================================ Beranda: mulai ====
function setStartMode(m) {
  $('#startMode').value = m;
  $$('#startSeg [data-sm]').forEach(b => b.classList.toggle('on', b.dataset.sm === m));
  $('#startTunda').hidden = m !== 'tunda'; $('#startSelesai').hidden = m !== 'selesai';
}
$('#startSeg').addEventListener('click', e => { const b = e.target.closest('[data-sm]'); if (b && !b.disabled) setStartMode(b.dataset.sm); });
$('#startMode').onchange = () => setStartMode($('#startMode').value);
function startBody(slot) {
  const mode = $('#startMode').value, body = { slot };
  if (mode === 'tunda') {
    const v = $('#startTunda').value || '00:30:00';
    body.tunda = v.length === 5 ? v + ':00' : v;
  } else if (mode === 'selesai') {
    const v = $('#startSelesai').value;
    if (!v) { toast('Isi jam selesai dulu', 'warn'); return null; }
    body.selesai = v;
  }
  return body;
}
let lastUsedName = '';
async function lastUsedLoad() {
  try {
    const r = await api('/api/history');
    const names = prosesList ? prosesList.slots.filter(s => s && s.name).map(s => s.name) : [];
    const h = (r.history || []).find(x => names.includes(x.proses));
    lastUsedName = h ? h.proses : '';
    const lu = $('#lastUsed'); lu.hidden = !lastUsedName; lu.textContent = lastUsedName ? 'Terakhir dipakai: ' + lastUsedName : '';
  } catch { }
}
function renderHomeProses() {
  if (!prosesList) return;
  const idle = !last || last.state === 'idle';
  const slots = prosesList.slots.map((s, i) => s ? { ...s, slot: i + 1 } : null).filter(Boolean);
  $('#homeEmpty').hidden = slots.length > 0;
  $('#startOpts').hidden = slots.length === 0;
  $('#prosesGrid').innerHTML = slots.map(s => s.bad
    ? `<div class="pcard prog" aria-disabled="true"><b class="nm">Slot ${s.slot}</b><small>Berkas rusak (hapus di menu Proses)</small></div>`
    : `<button class="pcard prog cpRun" data-run="${s.slot}" ${idle ? '' : 'disabled'}>${trailHtml(s.trail, s.count)}<div><div class="nm">${esc(s.name)}</div><div class="meta"><span>${s.count} Tahap</span><span>± ${fmtMin(s.est)}</span></div></div><span class="go">${ic('play', 'sm')}Mulai</span></button>`).join('');
  $$('#prosesGrid [data-run]').forEach(b => b.onclick = async () => {
    const body = startBody(+b.dataset.run); if (!body) return;
    try { toastR(await api('/api/run', 'POST', body)); pollOnce(); } catch (e) { guard(e); }
  });
}
$('#goProses').onclick = () => { location.hash = '#proses'; show('proses'); };

// ========================================== perangkat yang diingat ====
async function loadDevices() {
  let r; try { r = await api('/api/devices'); } catch { return; }
  if (!r.devices) return;
  $('#devHint').textContent = r.temporary
    ? 'Sesi ini TIDAK diingat (login ulang saat browser ditutup). Centang "Ingat perangkat ini" saat login.'
    : `Maksimal ${r.max} perangkat. Bila penuh, yang terlama otomatis dihapus. Perangkat tetap masuk sampai kamu menghapusnya.`;
  $('#devList').innerHTML = r.devices.map(d => `<div class="wifirow"><div><b>${esc(d.name)}</b> ${d.current ? '<span class="chip run">Perangkat ini</span>' : ''}<br><small>Masuk ${fmtDate(d.created)} · terakhir aktif ${fmtDate(d.lastSeen)}</small></div>
    <button class="btn dout sm" data-dev="${d.id}" data-cur="${d.current ? 1 : 0}">Hapus</button></div>`).join('') || '<p class="muted">Belum ada perangkat yang diingat.</p>';
  $$('#devList [data-dev]').forEach(b => b.onclick = async () => {
    if (!confirm(b.dataset.cur === '1' ? 'Hapus perangkat INI? Kamu akan keluar.' : 'Hapus perangkat ini dari daftar?')) return;
    const d = await api('/api/devices?id=' + b.dataset.dev, 'DELETE');
    toast(d.msg, d.ok ? 'ok' : 'warn');
    if (d.self) showLogin(); else loadDevices();
  });
}
$('#btnDevOthers').onclick = async () => {
  if (!confirm('Keluarkan semua perangkat lain?')) return;
  const d = await api('/api/devices?others=1', 'DELETE'); toast(d.msg, 'ok'); loadDevices();
};

// ================================================= tombol mata (password) ====
// Semua kolom password dibungkus dan diberi tombol tampil/sembunyi berikon sendiri (bukan bawaan peramban).
$$('input[type=password]').forEach(inp => {
  const wrap = document.createElement('span'); wrap.className = 'pwf';
  inp.parentNode.insertBefore(wrap, inp); wrap.appendChild(inp);
  const b = document.createElement('button'); b.type = 'button'; b.className = 'eye';
  const sync = () => { const sh = inp.type === 'text'; b.innerHTML = ic(sh ? 'eye-off' : 'eye'); b.setAttribute('aria-label', sh ? 'Sembunyikan password' : 'Tampilkan password'); b.setAttribute('aria-pressed', String(sh)); };
  b.onclick = e => { e.preventDefault(); inp.type = inp.type === 'password' ? 'text' : 'password'; sync(); inp.focus(); };
  sync(); wrap.appendChild(b);
});
// ============================================== zona seret-dan-lepas ====
// Klik membuka pemilih berkas (label untuk input); seret berkas ke zona ini memicu "change" yang sama.
$$('.drop').forEach(z => {
  const inp = $('input', z), dn = $('.dn', z);
  ['dragenter', 'dragover'].forEach(ev => z.addEventListener(ev, e => { e.preventDefault(); z.classList.add('over'); }));
  ['dragleave', 'drop'].forEach(ev => z.addEventListener(ev, e => { e.preventDefault(); z.classList.remove('over'); }));
  z.addEventListener('drop', e => {
    const f = e.dataTransfer && e.dataTransfer.files;
    if (!f || !f.length) return;
    try { inp.files = f; } catch { }
    inp.dispatchEvent(new Event('change', { bubbles: true }));
  });
  inp.addEventListener('change', () => { const f = inp.files && inp.files[0]; dn.textContent = f ? f.name : ''; z.classList.toggle('has', !!f); });
});

// ======================================================= koneksi ====
// Tanpa WebSocket (hemat memori ESP32): polling status + log tiap 2 dtk, 10 dtk saat tab tersembunyi.
let pollTimer = null, pollBusy = false;
async function pollOnce() {
  if (pollBusy) return;
  pollBusy = true;
  try {
    const r = await api('/api/poll?since=' + lastSeq);
    pollFails = 0;
    if (r.status) renderStatus(r.status);
    updateRemoteBanner(r.remote);
    addEntries(r.log.entries, true);
  } catch (e) { if (e.message !== 'login' && ++pollFails >= 2 && last) renderChips(last); } finally { pollBusy = false; }
}
function schedulePoll() {
  clearTimeout(pollTimer);
  pollTimer = setTimeout(async () => { if (!$('#app').hidden) await pollOnce(); schedulePoll(); }, document.hidden ? 10000 : 2000);
}
document.addEventListener('visibilitychange', () => { if (!document.hidden && !$('#app').hidden) { pollOnce(); schedulePoll(); } });
// Mode jarak jauh (Worker Cloudflare): kode UI sama, tetapi fitur sensitif disembunyikan.
function applyRemote(on) {
  REMOTE = on;
  $$('[data-local]').forEach(e => { e.hidden = on; });
  if (on && curSeg === 'konsol') actSeg('log');
}
function updateRemoteBanner(rm) {
  const b = $('#bannerOffline');
  if (!rm) { b.hidden = true; document.body.classList.remove('offline'); return; }
  b.hidden = rm.online;
  document.body.classList.toggle('offline', !rm.online);
  if (!rm.online) {
    const t = rm.lastSeen ? new Date(rm.lastSeen).toLocaleTimeString('id-ID', { hour: '2-digit', minute: '2-digit' }) : null;
    $('#offlineTxt').textContent = t ? `Mesin offline sejak ${t}. Kontrol dinonaktifkan sampai tersambung kembali.` : 'Mesin belum pernah terhubung ke server.';
  }
}
async function start() {
  let r;
  try { r = await api('/api/poll?since=0'); } catch { return; }
  $('#app').hidden = false; $('#login').hidden = true;
  try { const info = await api('/api/info'); applyRemote(!!(info && info.remote)); } catch { /* ESP32 lama tanpa /api/info */ }
  if (r.status) renderStatus(r.status);
  updateRemoteBanner(r.remote);
  addEntries(r.log.entries, false);
  if (r.log.entries.length >= 30) await fetchLogsAll();
  loadSettings(); loadMeta().then(() => { renderPalette(); return loadProses(); }); schedulePoll();
  const [v, sg] = location.hash.slice(1).split('/');
  if (VIEWS.includes(v) && v !== 'beranda') show(v, sg);
}
start();
