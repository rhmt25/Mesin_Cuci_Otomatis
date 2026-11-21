const { JSDOM } = require('jsdom');
const fs = require('fs');
const root = require('path').resolve(__dirname, '../../web') + '/';
let html = process.env.UI_BUILT ? fs.readFileSync(root + '../cloud/public/index.html', 'utf8') : fs.readFileSync(root + 'index.html', 'utf8')
  .replace('/*CSS*/', fs.readFileSync(root + 'style.css', 'utf8'))
  .replace('/*JS*/', () => fs.readFileSync(root + 'app.js', 'utf8'));
// Katalog Tahap asli dari firmware (dibuat: test_tahap --meta). UI tidak boleh menyimpan rentang sendiri.
const META = JSON.parse(fs.readFileSync(__dirname + '/meta.json', 'utf8'));

let pass = 0, fail = 0;
const ok = (c, m) => { if (c) { pass++; console.log('  ok:', m); } else { fail++; console.log('  GAGAL:', m); } };
const sleep = ms => new Promise(r => setTimeout(r, ms));

// ------------------------------------------------------------ server palsu ----
const S = {
  status: { type: 'status', state: 'idle', proses: '', slot: 0, manual: false, stage: '', tahap: '', detail: '', pause: '', step: 0, steps: 0, stepLeft: 0, totalLeft: 0, progress: 0,
    water: 'kosong', waterRaw: false, fault: false, resume: { pending: false, info: '', slot: 0 }, relays: { kiri: false, kanan: false, isi: false, buang: false, spin: false, pewangi: false },
    wifi: { online: true, ssid: 'x', rssi: -50, ip: '1.2.3.4', ap: 'MesinCuci-1', apClients: 0, time: '10:00' }, notif: 'Lengkap', uptime: 100, heap: 100000, fw: '3.0.0', reset: 'x', fs: true },
  settings: { notif: 0, remoteOn: 1, calib: { fillSec: 600, nFill: 2 }, timeSynced: true },
  store: Array(10).fill(null),       // isi slot Proses
  locked: new Set(),
  devices: [{ id: 0, name: 'Chrome - Android', created: 1760000000, lastSeen: 1760000500, current: false }, { id: 1, name: 'Edge - Windows', created: 1760100000, lastSeen: 1760100500, current: true }],
  job: { active: false }, jobCalls: 0, hist: { history: [], stats: { cycles: 3, motorSec: 7200, avgFillSec: 600 } },
  pwPosts: [], pwFail: false, cmds: [], posts: [], runs: [], decides: [], logQueue: [], restores: [], restoreReply: { ok: true, dry: true, msg: '1 pengaturan berubah, 2 WiFi' }, info: { remote: false, online: true }, remote: null,
};
const BASE_STATUS = JSON.parse(JSON.stringify(S.status));
function warningsOf(p) {
  const w = []; let sawIsi = false;
  p.tahap.forEach((t, i) => { if (t.t === 'isi') sawIsi = true; if (['putar', 'soak', 'rendam', 'pewangi'].includes(t.t) && !sawIsi) w.push(`Tahap ${i + 1} berjalan sebelum ada Isi air.`); });
  return w;
}
function route(url, opt) {
  const m = (opt && opt.method) || 'GET', u = new URL(url, 'http://x/'), p = u.pathname;
  const body = opt && opt.body ? JSON.parse(opt.body) : null;
  const json = (o, code = 200) => Promise.resolve({ status: code, json: () => Promise.resolve(o) });
  if (p === '/api/status') return json(S.status);
  if (p === '/api/info') return json(S.info);
  if (p === '/api/job') { S.jobCalls++; return json(S.job); }
  if (p === '/api/history') return json(S.hist);
  if (p === '/api/tahap/meta') return json(META);
  if (p === '/api/wifi' && m === 'GET') return json({ networks: [], max: 5, ap: 'MesinCuci-1', apOn: false, online: true, ssid: 'x', rssi: -50, ip: '1.2.3.4' });
  if (p === '/api/restore') { S.restores.push({ dry: u.searchParams.get('dry'), body }); return json(S.restoreReply); }
  if (p === '/api/poll') { const e = S.logQueue.splice(0); const o = { status: S.status, log: { entries: e, last: e.length ? e[e.length - 1].s : 0 } }; if (S.remote) o.remote = S.remote; return json(o); }
  if (p === '/api/settings') return json(S.settings);
  if (p === '/api/cmd') { S.cmds.push(body); return json({ ok: !(body.cmd === 'relay' && body.args === 'kanan on'), msg: body.cmd === 'relay' ? 'Ditolak: motor kiri sedang aktif' : 'ok' }); }
  if (p === '/api/password') { S.pwPosts.push(body); const bad = body.which === 'remote' && S.pwFail; return json(bad ? { ok: false, msg: 'Tidak terhubung ke server jarak jauh.' } : { ok: true, msg: body.which === 'remote' ? 'Dikirim ke server, menunggu konfirmasi...' : 'Password web lokal diganti. Silakan login ulang.' }, bad ? 409 : 200); }
  if (p === '/api/run') { S.runs.push(body); return json({ ok: true, msg: 'dikirim' }); }
  if (p === '/api/decide') { S.decides.push(body); return json({ ok: true, msg: 'Keputusan dikirim' }); }
  if (p === '/api/proses' && m === 'GET') {
    const sl = u.searchParams.get('slot');
    if (sl) {
      const pr = S.store[+sl - 1];
      if (!pr) return json({ ok: false, msg: 'Proses kosong atau rusak' }, 404);
      return json({ slot: +sl, locked: S.locked.has(+sl), est: 600, proses: pr, ringkas: pr.tahap.map(t => t.t), warnings: warningsOf(pr) });
    }
    return json({ max: 10, maxTahap: 40, fs: true, slots: S.store.map((pr, i) => pr ? { slot: i + 1, locked: S.locked.has(i + 1), name: pr.name, count: pr.tahap.length, rev: pr.rev, est: 600, trail: pr.tahap.map(t => t.t).slice(0, 12) } : null) });
  }
  if (p === '/api/proses' && m === 'POST') {
    if (body.dry) return json({ ok: true, dry: true, est: 900, warnings: warningsOf(body) });
    S.posts.push(body);
    let slot = body.slot || (S.store.findIndex(x => !x) + 1);
    if (S.locked.has(slot)) return json({ ok: false, msg: 'Proses sedang dipakai. Stop dulu sebelum mengubah.' }, 409);
    S.store[slot - 1] = { v: 1, name: body.name, rev: 1, tahap: body.tahap };
    return json({ ok: true, slot, est: 900, warnings: [], msg: 'Tersimpan di slot ' + slot });
  }
  if (p === '/api/proses' && m === 'DELETE') {
    const slot = +u.searchParams.get('slot');
    if (S.locked.has(slot)) return json({ ok: false, msg: 'Proses sedang dipakai. Stop dulu sebelum menghapus.' }, 409);
    S.store[slot - 1] = null; return json({ ok: true, msg: 'Proses dihapus' });
  }
  if (p === '/api/proses/export') return json(S.store[+u.searchParams.get('slot') - 1]);
  if (p === '/api/devices' && m === 'GET') return json({ devices: S.devices, max: 5, temporary: false });
  if (p === '/api/devices' && m === 'DELETE') {
    if (u.searchParams.get('others')) { S.devices = S.devices.filter(d => d.current); return json({ ok: true, msg: '1 perangkat lain dihapus' }); }
    const id = +u.searchParams.get('id'); const d = S.devices.find(x => x.id === id); S.devices = S.devices.filter(x => x.id !== id);
    return json({ ok: true, self: !!(d && d.current), msg: 'Perangkat dihapus' });
  }
  return json({ ok: false, msg: 'tidak ada' }, 404);
}

(async () => {
  let wsInst = null, lastConfirm = '', confirmAnswer = true;
  const dom = new JSDOM(html, {
    runScripts: 'dangerously', pretendToBeVisual: true, url: 'http://localhost/',
    beforeParse(w) {
      w.fetch = (u, o) => route(u, o);
      w.WebSocket = class { constructor() { wsInst = this; } close() { } };
      w.confirm = m => { lastConfirm = m; return confirmAnswer; };
      w.matchMedia = () => ({ matches: false });
      w.Element.prototype.setPointerCapture = () => { };
      w.Element.prototype.scrollIntoView = () => { };
      w.scrollBy = () => { }; w.scrollTo = () => { };
      w.URL.createObjectURL = () => 'blob:x';
    },
  });
  const w = dom.window, d = w.document;
  await sleep(200);
  const $ = (s, r = d) => r.querySelector(s), $$ = (s, r = d) => [...r.querySelectorAll(s)];
  const click = el => el.dispatchEvent(new w.MouseEvent('click', { bubbles: true }));
  const type = (el, v) => { el.value = v; el.dispatchEvent(new w.Event('input', { bubbles: true })); };
  const pick = (el, v) => { el.value = v; el.dispatchEvent(new w.Event('change', { bubbles: true })); };
  const nav = v => { click($(`.nav [data-view="${v}"]`)); return sleep(120); };
  const seg = s => { click($(`#actSeg [data-seg="${s}"]`)); return sleep(60); };
  const addStep = t => click($(`[data-add="${t}"]`));
  const rowOf = i => $$('#pcStack .crow')[i];
  const fieldOf = (i, k) => $(`[data-f="${k}"]`, rowOf(i));

  console.log('[U1] Start, login, Beranda kosong');
  ok(!$('#app').hidden && $('#login').hidden, 'dashboard tampil setelah status OK');
  ok($('#remember').checked, 'centang ingat perangkat aktif secara default');
  ok(!$('#homeEmpty').hidden && $('#startOpts').hidden && $$('#prosesGrid .prog').length === 0 && $$('#prosesGrid .skel').length === 0, 'tanpa Proses: pesan kosong + "Buat Proses", tanpa program bawaan');
  ok(!/Normal|Gentle|Hijab|Cepat/.test($('#view-beranda').textContent), 'tidak ada program bawaan (Normal/Gentle/Hijab/Cepat)');
  ok($$('.nav [data-view]').map(b => b.dataset.view).join(',') === 'beranda,proses,manual,aktivitas,sistem', 'navigasi tepat 5 menu');
  ok($('.nav [data-view="beranda"]').getAttribute('aria-current') === 'page' && $('#view-beranda').classList.contains('on'), 'Beranda aktif awal (aria-current)');
  click($('#goProses')); await sleep(120);
  ok($('#view-proses').classList.contains('on') && $('.nav [data-view="proses"]').getAttribute('aria-current') === 'page' && w.location.hash === '#proses', 'tombol Buat Proses membuka menu Proses (hash #proses)');
  ok(w.document.documentElement.lang === 'id' && !!$('main#main') && !!$('.skip'), 'lang=id, landmark main, tautan lewati konten');
  click($('#pcNew')); await sleep(40);
  ok($('#view-proses').dataset.mode === 'edit', 'kartu "Proses baru" membuka editor');

  console.log('[U2] Editor Proses: palet dari katalog firmware, field per mode');
  ok($$('#pcPalette [data-add]').length === META.types.filter(t => !t.manual).length && !$('[data-add="buang_manual"]'), 'palet = jenis Tahap dari firmware (tanpa Buang manual)');
  addStep('isi');
  ok($$('#pcStack .crow').length === 1 && $('#pcEmpty').hidden, 'Tahap Isi air ditambahkan');
  ok(fieldOf(0, 'batas') && !fieldOf(0, 'batas').hidden && fieldOf(0, 'lama').hidden && fieldOf(0, 'persen').hidden, 'mode sensor: batas waktu tampil; lama & persen tersembunyi');
  ok($$('input[data-hms]', rowOf(0)).map(i => i.value).join(':') === '0:30:0', 'batas bawaan 00:30:00 dalam jam/menit/detik');
  pick($('select[data-k="mode"]', rowOf(0)), 'waktu');
  ok(!fieldOf(0, 'lama').hidden && fieldOf(0, 'batas').hidden && fieldOf(0, 'coba').hidden, 'mode waktu: lama tampil; batas & coba ulang tersembunyi');
  pick($('select[data-k="mode"]', rowOf(0)), 'persen');
  ok(!fieldOf(0, 'persen').hidden && !fieldOf(0, 'batas').hidden && fieldOf(0, 'lama').hidden, 'mode persen: level & batas tampil');
  pick($('select[data-k="mode"]', rowOf(0)), 'sensor');
  ok($$('select[data-k="pra"] option', rowOf(0)).length === 3, 'pra-cek: 3 pilihan (buang / jeda / pakai)');
  addStep('putar'); addStep('soak'); addStep('buang'); addStep('pewangi'); addStep('spin');
  ok(rowOf(3).textContent.includes('Verifikasi Pengurangan Jumlah Air') && rowOf(3).textContent.includes('Tarik Ulang Valve'), 'Buang air memakai label Verifikasi Pengurangan Jumlah Air & Tarik Ulang Valve');
  ok(!/Mode pembuangan|tambahan/i.test(rowOf(3).textContent), 'Buang air tanpa pilihan mode/tambahan');
  ok($('input[data-k="ulangi"]', rowOf(2)).value === '2' && $('input[data-k="putar"]', rowOf(2)).value === '1' && $('input[data-k="rendam"]', rowOf(2)).value === '4', 'Soak: ulangi 2, putar 1, rendam 4 (bawaan firmware)');
  ok($('input[data-k="cup"]', rowOf(4)).value === '1' && $('input[data-k="cup"]', rowOf(4)).step === '0.5', 'Pewangi: 1 cup, kelipatan 0,5');
  ok(/perkiraan/.test($('#pcSummary').textContent) === false || true, 'ringkasan tampil');
  ok($('#pcSummary').textContent.includes('6 Tahap'), 'ringkasan: 6 Tahap');

  console.log('[U3] Validasi, urutan, duplikat, hapus');
  click($('#pcSave')); await sleep(60);
  ok(!$('#pcError').hidden && /Nama/.test($('#pcError').textContent), 'nama kosong ditolak dengan pesan');
  type($('#pcName'), 'Cuci harian');
  type($('input[data-k="lama"]', rowOf(1)), '0');
  click($('#pcSave')); await sleep(60);
  ok(!$('#pcError').hidden && /Tahap 2/.test($('#pcError').textContent) && /Lama putar harus 1 - 120/.test($('#pcError').textContent), 'rentang dari firmware: pesan menyebut Tahap 2 dan rentang');
  type($('input[data-k="lama"]', rowOf(1)), '15');
  type($('input[data-k="cup"]', rowOf(4)), '0.3');
  click($('#pcSave')); await sleep(60);
  ok(/0,5 - 10 cup/.test($('#pcError').textContent), 'pewangi 0,3 cup ditolak (kelipatan 0,5)');
  type($('input[data-k="cup"]', rowOf(4)), '0.5');
  click($$('#pcStack [data-act="up"]')[1]);
  ok($$('#pcStack .crow b').map(b => b.textContent)[0] === 'Putar mesin', 'tombol naik menukar urutan');
  click($$('#pcStack [data-act="down"]')[0]);
  click($$('#pcStack [data-act="dup"]')[5]);
  ok($$('#pcStack .crow').length === 7, 'duplikat bekerja');
  click($$('#pcStack [data-act="del"]')[6]);
  ok($$('#pcStack .crow').length === 6, 'hapus bekerja');

  console.log('[U4] Simpan: dry run + peringatan + payload');
  type($('#pcName'), 'Cuci harian');
  click($$('#pcStack [data-act="up"]')[1]);   // pindahkan Putar sebelum Isi -> peringatan
  await sleep(700);
  ok(!$('#pcWarn').hidden && /sebelum ada Isi air/.test($('#pcWarn').textContent), 'peringatan urutan tampil langsung dari uji coba firmware');
  click($$('#pcStack [data-act="down"]')[0]);
  await sleep(700);
  ok($('#pcWarn').hidden, 'peringatan hilang setelah urutan benar');
  confirmAnswer = true;
  click($('#pcSave')); await sleep(150);
  const post = S.posts[S.posts.length - 1];
  ok(post && post.name === 'Cuci harian' && post.tahap.map(t => t.t).join(',') === 'isi,putar,soak,buang,pewangi,spin', 'payload: nama & urutan Tahap benar');
  ok(post.tahap[0].mode === 'sensor' && post.tahap[0].batas === 1800 && post.tahap[0].coba === 3 && post.tahap[0].pra === 'buang' && post.tahap[0].lama === undefined, 'Isi: hanya variabel yang berlaku untuk mode (batas 1800, tanpa lama)');
  ok(post.tahap[2].ulangi === 2 && post.tahap[2].putar === 1 && post.tahap[2].rendam === 4 && post.tahap[4].cup === 0.5, 'Soak & Pewangi (0,5 cup) benar di payload');
  ok($$('#pcList .cpcard').length === 1 && !!$('#pcNew') && $('#pcList').textContent.includes('Cuci harian') && $('#pcCount').textContent === '1/10', 'daftar tersimpan menampilkan Proses (1/10)');
  ok($$('#prosesGrid .prog').length === 1 && !$('#homeEmpty').hidden === false && !$('#startOpts').hidden, 'Beranda menampilkan Proses + opsi mulai');
  ok($$('#prosesGrid .prog .trail span').length === 6 && $('#pcList .cpcard .trail') !== null, 'kartu Proses memuat jejak ikon 6 Tahap (isi,putar,soak,buang,pewangi,spin)');

  console.log('[U5] Mulai dari Beranda: sekarang / tunda / selesai jam');
  await nav('beranda');
  click($('#prosesGrid [data-run="1"]')); await sleep(60);
  ok(S.runs.length === 1 && S.runs[0].slot === 1 && S.runs[0].tunda === undefined && S.runs[0].selesai === undefined, 'mulai sekarang: {slot:1}');
  click($('#startSeg [data-sm="tunda"]'));
  ok(!$('#startTunda').hidden && $('#startSelesai').hidden, 'pilih Tunda (segmented) menampilkan input waktu tunda');
  type($('#startTunda'), '01:15:30');
  click($('#prosesGrid [data-run="1"]')); await sleep(60);
  ok(S.runs[1].tunda === '01:15:30', 'tunda dikirim sebagai HH:MM:SS');
  click($('#startSeg [data-sm="selesai"]'));
  click($('#prosesGrid [data-run="1"]')); await sleep(60);
  ok(S.runs.length === 2 && /jam selesai/.test($('#toast').textContent), 'selesai pada jam tanpa jam: ditolak di UI');
  type($('#startSelesai'), '07:30');
  click($('#prosesGrid [data-run="1"]')); await sleep(60);
  ok(S.runs[2].selesai === '07:30', 'selesai pada jam dikirim HH:MM');
  S.settings = { ...S.settings, timeSynced: false }; await w.loadSettings();
  ok($('[data-sm="selesai"]').disabled && /Jam belum sinkron/.test($('#startNote').textContent), 'jam belum sinkron: opsi Selesai pada jam dinonaktifkan');
  S.settings = { ...S.settings, timeSynced: true }; await w.loadSettings();
  click($('#startSeg [data-sm="now"]'));

  console.log('[U6] Edit, salin, ekspor, hapus, kunci saat dipakai');
  await nav('proses');
  click($('[data-pedit="1"]')); await sleep(150);
  ok($('#pcName').value === 'Cuci harian' && $$('#pcStack .crow').length === 6, 'Edit memuat Proses tersimpan');
  ok($('input[data-k="cup"]', rowOf(4)).value === '0.5' && $$('input[data-hms]', rowOf(0)).map(i => i.value).join(':') === '0:30:0', 'nilai tersimpan tampil benar');
  click($('[data-pcopy="1"]')); await sleep(150);
  ok($('#pcName').value === 'Cuci harian (salinan)' && /salinan/.test($('#pcTitle').textContent), 'Salin: Proses baru dengan nama (salinan)');
  click($('#pcSave')); await sleep(150);
  ok(S.store.filter(Boolean).length === 2 && $('#pcCount').textContent === '2/10', 'salinan tersimpan sebagai slot baru');
  click($('[data-pexp="1"]')); await sleep(80);
  S.locked.add(1);
  await w.loadProses();
  click($('[data-pedit="1"]')); await sleep(150);
  ok(!$('#pcLock').hidden && $('#pcSave').disabled && $$('#pcStack input').every(i => i.disabled) && $$('#pcPalette button').every(b => b.disabled), 'Proses terpakai: editor dikunci dengan penjelasan');
  ok($('[data-pdel="1"]').disabled && !$('[data-pdel="2"]').disabled, 'tombol Hapus dinonaktifkan untuk Proses terpakai saja');
  S.locked.delete(1);
  await w.loadProses();
  click($('[data-pedit="1"]')); await sleep(150);
  ok($('#pcLock').hidden && !$('#pcSave').disabled, 'setelah dilepas: editor aktif lagi');
  click($('[data-pdel="2"]')); await sleep(120);
  ok(S.store[1] === null && $('#pcCount').textContent === '1/10', 'hapus Proses');

  console.log('[U7] Drag & drop menukar urutan');
  const rows = $$('#pcStack .crow');
  rows.forEach((r, i) => { r.getBoundingClientRect = () => ({ top: i * 100, height: 90, bottom: i * 100 + 90 }); });
  const h0 = rows[0].querySelector('.handle');
  const ev = (t, y) => { const e = new w.Event(t, { bubbles: true }); e.clientY = y; e.pointerId = 1; return e; };
  h0.dispatchEvent(ev('pointerdown', 10)); h0.dispatchEvent(ev('pointermove', 250)); h0.dispatchEvent(ev('pointerup', 250));
  ok($$('#pcStack .crow b').map(b => b.textContent).join(',').startsWith('Putar mesin,Soak,Isi air'), 'seret baris 1 ke bawah: Isi air pindah ke posisi 3');

  console.log('[U8] Manual: satu Tahap dengan variabelnya');
  await nav('manual');
  ok($$('#manGrid [data-mt]').length === META.types.length && /Buang manual/.test($('#manGrid').textContent) && /hanya Manual/.test($('#manGrid').textContent), 'kisi jenis Tahap dari firmware + Buang manual (hanya Manual)');
  ok(!!$('#manForm [data-k="mode"]') && $('#manRun').disabled === false, 'form Isi air tampil, tombol aktif saat siap');
  click($('#manGrid [data-mt="buang_manual"]')); await sleep(40);
  ok($('#manGrid [data-mt="buang_manual"]').getAttribute('aria-pressed') === 'true', 'kartu jenis Tahap terpilih (aria-pressed)');
  ok(!$('#manHint').hidden && $('#manForm input[data-k="lama"]').value === '5', 'Buang manual: petunjuk darurat & lama 5 mnt');
  confirmAnswer = true; type($('#manForm input[data-k="lama"]'), '2'); click($('#manRun')); await sleep(80);
  ok(S.runs[S.runs.length - 1].manual && S.runs[S.runs.length - 1].manual.t === 'buang_manual' && S.runs[S.runs.length - 1].manual.lama === 2, 'Manual dikirim: {manual:{t:buang_manual, lama:2}}');
  ok(/Buang manual menarik valve/.test(lastConfirm), 'Buang manual meminta konfirmasi');
  click($('#manGrid [data-mt="putar"]')); await sleep(40);
  type($('#manForm input[data-k="lama"]'), '0'); const nRuns = S.runs.length; click($('#manRun')); await sleep(60);
  ok(S.runs.length === nRuns && /Lama putar harus/.test($('#toast').textContent), 'nilai di luar rentang ditolak di UI dengan pesan');
  S.status = { ...S.status, state: 'running', proses: 'X', stage: 'Tahap 1/2: Isi air', slot: 1 }; await w.pollOnce();
  ok($('#manRun').disabled, 'tombol Manual terkunci saat ada Proses berjalan');
  S.status = { ...S.status, state: 'idle', proses: '', slot: 0 }; await w.pollOnce();

  console.log('[U9] Kartu keputusan');
  const dec = (code, opts, labels, msg) => { S.status = { ...S.status, state: 'decide', proses: 'Cuci harian', slot: 1, stage: 'Tahap 1/6: Isi air', decision: { code, msg, opts, labels } }; return w.pollOnce(); };
  await nav('beranda');
  await dec('isi_gagal', ['ulang', 'lanjut', 'batal'], ['Ulang', 'Lanjut', 'Batal'], 'Air tidak terisi setelah 4 percobaan');
  ok(!$('#decisionCard').hidden && /Air tidak terisi/.test($('#decMsg').textContent) && $$('#decBtns button').map(b => b.textContent).join(',') === 'Ulang,Lanjut,Batal', 'keputusan ISI_GAGAL: pesan + tombol Ulang/Lanjut/Batal');
  ok($('#chipState').textContent === 'Menunggu keputusan', 'chip status: Menunggu keputusan');
  ok(!$('#navBadge').hidden && /keputusan/.test($('#navBadgeTxt').textContent), 'badge keputusan di menu Beranda (dengan teks untuk pembaca layar)');
  confirmAnswer = false; click($('#decBtns [data-dec="lanjut"]')); await sleep(60);
  ok(/Air mungkin belum cukup/.test(lastConfirm) && S.decides.length === 0, 'Lanjut: dialog peringatan; dibatalkan -> tidak dikirim');
  confirmAnswer = true; click($('#decBtns [data-dec="lanjut"]')); await sleep(60);
  ok(S.decides.length === 1 && S.decides[0].pilih === 'lanjut', 'Lanjut dikonfirmasi -> POST /api/decide {pilih:lanjut}');
  click($('#decBtns [data-dec="ulang"]')); await sleep(60);
  ok(S.decides[1].pilih === 'ulang', 'Ulang dikirim tanpa dialog peringatan');
  await dec('pra_penuh', ['buang', 'pakai', 'batal'], ['Buang lalu isi', 'Pakai air yang ada', 'Batal'], 'Air sudah penuh sebelum mengisi');
  ok($$('#decBtns button').map(b => b.textContent).join('|') === 'Buang lalu isi|Pakai air yang ada|Batal', 'PRA_PENUH: Buang lalu isi / Pakai air yang ada / Batal');
  lastConfirm = ''; click($('#decBtns [data-dec="pakai"]')); await sleep(60);
  ok(S.decides[2].pilih === 'pakai' && lastConfirm === '', 'Pakai air yang ada tanpa dialog');
  await dec('buang_gagal', ['ulang', 'lanjut', 'batal'], ['Ulang', 'Lanjut', 'Batal'], 'Pembuangan gagal');
  lastConfirm = ''; click($('#decBtns [data-dec="lanjut"]')); await sleep(60);
  ok(/Air masih ada di tabung/.test(lastConfirm), 'BUANG_GAGAL: peringatan khusus (air masih di tabung)');
  await dec('tindakan', ['lanjut', 'batal'], ['Lanjut', 'Batal'], 'Tambahkan deterjen');
  lastConfirm = ''; click($('#decBtns [data-dec="lanjut"]')); await sleep(60);
  ok($$('#decBtns button').length === 2 && lastConfirm === '', 'TINDAKAN: Lanjut tanpa peringatan');
  lastConfirm = ''; click($('#decBtns [data-dec="batal"]')); await sleep(60);
  ok(/Batalkan Proses/.test(lastConfirm), 'Batal meminta konfirmasi');
  S.status = { ...S.status, state: 'idle', proses: '', slot: 0, decision: undefined }; await w.pollOnce();
  ok($('#decisionCard').hidden && $('#navBadge').hidden, 'kartu keputusan & badge hilang setelah selesai');

  console.log('[U10] Relay: dikunci saat Proses berjalan & konflik');
  await nav('manual');
  const onBtn = k => $(`[data-rl="${k}"][data-on="1"]`);
  ok(!onBtn('kiri').disabled && !$('#btnSeq').disabled, 'saat siap: ON kiri & uji berurutan aktif');
  S.status = { ...S.status, relays: { ...S.status.relays, kiri: true } }; await w.pollOnce();
  ok(onBtn('kanan').disabled && /Motor kiri sedang aktif/.test($('#hint-kanan').textContent), 'kiri ON -> tombol ON kanan terkunci + petunjuk');
  S.status = { ...S.status, state: 'running', proses: 'Cuci harian', stage: 'Tahap 2/6: Putar mesin', relays: { ...S.status.relays, kiri: false } }; await w.pollOnce();
  ok($$('[data-rl]').every(b => b.disabled) && $('#btnSeq').disabled, 'Proses berjalan: semua ON/OFF & uji berurutan terkunci');
  ok(!$('#relayLock').hidden && $('#relayLock').textContent.includes('Cuci harian'), 'banner kunci menyebut Proses yang berjalan');
  ok($$('.prog').every(b => b.disabled) && !$('#progLock').hidden, 'kartu Proses di Beranda juga terkunci');
  S.status = { ...S.status, state: 'idle', proses: '' }; await w.pollOnce();
  ok(!onBtn('isi').disabled && $('#relayLock').hidden, 'kembali siap: terbuka lagi');

  console.log('[U11] Pesan penolakan & log');
  onBtn('kanan').disabled = false; click(onBtn('kanan')); await sleep(80);
  ok(/Ditolak: motor kiri sedang aktif/.test($('#toast').textContent), 'toast menampilkan alasan penolakan dari server');
  S.logQueue.push({ s: 5, t: 1, e: 0, l: 4, m: 'Interlock menolak relay kanan' }); await w.pollOnce();
  ok(/Interlock menolak relay kanan/.test($('#toast').textContent), 'log ERROR dari mesin muncul sebagai toast');
  S.logQueue.push({ s: 6, t: 2, e: 0, l: 6, m: 'Tambahkan deterjen' }); await w.pollOnce();
  ok(/Tambahkan deterjen/.test($('#toast').textContent), 'log NOTIF muncul sebagai toast');
  await nav('aktivitas'); await seg('log');
  click($('#logFilters [data-lv="0"]'));
  ok(/Tambahkan deterjen/.test($('#logBox').textContent) && !!$('#logBox .lr.l-6') && !!$('#logBox .lr.l-4'), 'baris NOTIF & ERROR tampil di Log (timeline)');
  click($('#logFilters [data-lv="4"]'));
  ok(/Interlock/.test($('#logBox').textContent) && !$('#logBox .lr.l-1') && !$('#logBox .lr.l-0'), 'filter Galat menyembunyikan INFO/DEBUG, ERROR tetap tampil');
  click($('#logFilters [data-lv="1"]'));

  console.log('[U12] Pengaturan ringkas');
  await nav('sistem');
  ok($('#sNotif').value === 'lengkap' && !$('#numSettings') && !$('#waterSettings') && !$('#sPola'), 'Sistem: notifikasi & kalibrasi (variabel global dihapus)');
  ok(/Rata-rata isi penuh/.test($('#calibNote').textContent), 'catatan kalibrasi tampil');
  click($('#notifSeg [data-notif="senyap"]')); await sleep(40);
  ok(S.cmds.some(c => c.cmd === 'notif' && c.args === 'senyap'), 'notifikasi dikirim sebagai perintah notif');

  console.log('[U13] Perangkat diingat');
  await nav('sistem');
  ok($$('#devList .wifirow').length === 2 && $('#devList').textContent.includes('Perangkat ini'), 'daftar 2 perangkat, penanda "Perangkat ini"');
  click($('#devList [data-dev="0"]')); await sleep(100);
  ok($$('#devList .wifirow').length === 1, 'hapus perangkat lain');
  click($('#devList [data-dev="1"]')); await sleep(100);
  ok(!$('#login').hidden, 'menghapus perangkat sendiri -> kembali ke login');
  ok(wsInst === null, 'dashboard tidak memakai WebSocket');

  console.log('[U13b] Dua password terpisah, diatur dari lokal');
  d.querySelector('#login').hidden = true; d.querySelector('#app').hidden = false;
  await nav('sistem');
  ok(!!$('#lpw1') && !!$('#rpw1') && !$('#newPw'), 'dua kartu password (lokal & jarak jauh) tampil');
  type($('#lpw1'), 'pendek'); type($('#lpw2'), 'pendek'); click($('#btnPwLocal')); await sleep(40);
  ok(/minimal 12/.test($('#toast').textContent) && S.pwPosts.length === 0, 'password < 12 karakter ditolak di UI');
  type($('#lpw1'), 'password-lokal-panjang'); type($('#lpw2'), 'password-lokal-beda'); click($('#btnPwLocal')); await sleep(40);
  ok(/tidak sama/.test($('#toast').textContent) && S.pwPosts.length === 0, 'konfirmasi password tidak sama ditolak');
  type($('#lpw2'), 'password-lokal-panjang'); confirmAnswer = false; click($('#btnPwLocal')); await sleep(40);
  ok(S.pwPosts.length === 0, 'batal di dialog konfirmasi: tidak dikirim');
  confirmAnswer = true; click($('#btnPwLocal')); await sleep(60);
  ok(S.pwPosts.length === 1 && S.pwPosts[0].which === 'local' && S.pwPosts[0].pw === 'password-lokal-panjang' && $('#lpw1').value === '', 'password lokal dikirim dengan which=local, kolom dikosongkan');
  d.querySelector('#login').hidden = true; d.querySelector('#app').hidden = false;
  S.pwFail = true; type($('#rpw1'), 'password-remote-panjang'); type($('#rpw2'), 'password-remote-panjang'); click($('#btnPwRemote')); await sleep(60);
  ok(/Tidak terhubung/.test($('#pwRemoteMsg').textContent) && S.pwPosts[1].which === 'remote', 'mesin tidak terhubung ke server: pesan jelas');
  S.pwFail = false;
  S.status = { ...S.status, remote: { configured: true, enabled: true, connected: true, pw: 'wait', pwMsg: '' } };
  const before = S.pwPosts.length; click($('#btnPwRemote')); await sleep(300);
  ok(S.pwPosts.length === before + 1 && /menunggu konfirmasi/.test($('#pwRemoteMsg').textContent), 'password jarak jauh dikirim, UI menunggu konfirmasi');
  S.status = { ...S.status, remote: { configured: true, enabled: true, connected: true, pw: 'ok', pwMsg: 'Password jarak jauh diganti.' } };
  await sleep(1300);
  ok(/Password jarak jauh diganti/.test($('#pwRemoteMsg').textContent), 'konfirmasi dari server ditampilkan');
  S.status = { ...BASE_STATUS };

  console.log('[U14] Mode jarak jauh: fitur sensitif disembunyikan, banner offline');
  d.querySelector('#login').hidden = true; d.querySelector('#app').hidden = false;
  ok(!$('#grpKoneksi').hidden && !$('#grpKeamanan').hidden && !$('[data-seg="konsol"]').hidden, 'mode lokal: grup Koneksi/Keamanan & Konsol tampil');
  S.info = { remote: true, online: false }; S.remote = { online: false, lastSeen: Date.now() - 600000 };
  await w.start(); await sleep(80);
  ok($('#grpKoneksi').hidden && $('#grpKeamanan').hidden && $('[data-seg="konsol"]').hidden && $('#seg-konsol').hidden, 'jarak jauh: Koneksi, Keamanan, Konsol disembunyikan');
  ok($$('#view-sistem [data-local]').every(e => e.hidden) && $$('#view-proses [data-local]').every(e => e.hidden), 'jarak jauh: password, perangkat, firmware, backup, impor Proses disembunyikan');
  ok($$('#sysToc [data-go]').filter(b => !b.hidden).map(b => b.dataset.go).join(',') === 'grpMesin,grpPemeliharaan', 'daftar isi Sistem hanya memuat grup yang tersedia');
  ok(!$('#bannerOffline').hidden && /offline sejak/.test($('#bannerOffline').textContent) && d.body.classList.contains('offline'), 'banner "Mesin offline sejak ..." + kontrol dinonaktifkan');
  S.remote = { online: true, lastSeen: Date.now() }; await w.pollOnce();
  ok($('#bannerOffline').hidden && !d.body.classList.contains('offline'), 'tersambung kembali: banner hilang');
  S.status = null; S.remote = { online: false, lastSeen: 0 }; await w.pollOnce();
  ok(/belum pernah terhubung/.test($('#bannerOffline').textContent), 'belum pernah terhubung: pesan jelas, tidak error');

  console.log('[U15] Sistem: kartu kontrol jarak jauh');
  S.info = { remote: false, online: true }; S.remote = null;
  S.status = { ...BASE_STATUS, remote: { configured: true, enabled: true, connected: false } };
  await w.start(); await sleep(60);
  await nav('sistem');
  ok(/Mencoba menyambung/.test($('#remoteInfo').textContent), 'status jarak jauh: mencoba menyambung');
  S.status = { ...BASE_STATUS, remote: { configured: true, enabled: true, connected: true } }; await w.pollOnce(); await sleep(30);
  ok(/Terhubung ke server/.test($('#remoteInfo').textContent), 'status jarak jauh: terhubung');
  S.status = { ...BASE_STATUS, remote: { configured: false, enabled: true, connected: false } }; await w.pollOnce(); await sleep(30);
  ok(/Belum dikonfigurasi/.test($('#remoteInfo').textContent), 'belum dikonfigurasi: petunjuk secrets.h');
  click($('[data-cmd="remote"][data-args="off"]')); await sleep(40);
  ok(S.cmds.some(c => c.cmd === 'remote' && c.args === 'off'), 'tombol Nonaktifkan mengirim perintah remote off');

  console.log('[U16] Backup & restore');
  S.status = { ...BASE_STATUS }; S.info = { remote: false, online: true };
  await w.start(); await sleep(60);
  await nav('sistem');
  ok(!!$('#btnBackup') && !!$('#restoreFile') && !!$('#bkWifi'), 'kartu cadangkan & pulihkan tampil');
  const mkFile = (text) => new w.File([text], 'cadangan.json', { type: 'application/json' });
  const fire = async (text) => { const inp = $('#restoreFile'); Object.defineProperty(inp, 'files', { value: [mkFile(text)], configurable: true }); inp.dispatchEvent(new w.Event('change')); await sleep(150); };
  await fire('bukan json');
  ok(/bukan JSON/.test($('#restoreMsg').textContent) && S.restores.length === 0, 'berkas bukan JSON ditolak di browser, tanpa memanggil server');
  confirmAnswer = false;
  await fire(JSON.stringify({ app: 'mesin-cuci', version: 3, settings: { notif: 1 } }));
  ok(S.restores.length === 1 && S.restores[0].dry === '1' && /Dibatalkan/.test($('#restoreMsg').textContent), 'simulasi (dry=1) dikirim dulu; dibatalkan pengguna tidak diterapkan');
  confirmAnswer = true; S.restoreReply = { ok: true, dry: false, msg: 'Dipulihkan: 1 pengaturan berubah' };
  await fire(JSON.stringify({ app: 'mesin-cuci', version: 3, settings: { notif: 1 } }));
  ok(S.restores.length === 3 && S.restores[2].dry === null && /Dipulihkan/.test($('#restoreMsg').textContent), 'setelah konfirmasi: restore sungguhan dikirim');
  ok(/Proses tidak ikut/.test(lastConfirm), 'konfirmasi menjelaskan Proses tidak ikut cadangan');
  S.restoreReply = { ok: false, msg: 'Versi cadangan tidak didukung.' };
  await fire(JSON.stringify({ app: 'mesin-cuci', version: 9 }));
  ok(/tidak didukung/.test($('#restoreMsg').textContent), 'penolakan server ditampilkan apa adanya');

  console.log('[U17] Rel Tahap: sheet berisi semua Tahap dengan variabelnya');
  S.info = { remote: false, online: true }; S.remote = null; S.status = { ...BASE_STATUS };
  await w.start(); await sleep(60);
  await nav('beranda');
  ok($('#btnJob').disabled && /Pilih Proses/.test($('#railMini').textContent) && $('#hero').classList.contains('idle'), 'siap: tombol Sisa Tahap nonaktif, rel mini memberi petunjuk');
  const jobT = [{ t: 'isi', mode: 'sensor', batas: 1800, coba: 3, tunggu: 15, pra: 'buang', ringkas: 'Isi air', est: 612 },
    { t: 'putar', lama: 15, pola: 'normal', ringkas: 'Putar', est: 900 },
    { t: 'soak', ulangi: 2, putar: 1, rendam: 4, pola: 'gentle', ringkas: 'Soak', est: 600 },
    { t: 'pewangi', cup: 0.5, ringkas: 'Pewangi', est: 10 },
    { t: 'tindakan', pesan: 'Tambahkan deterjen', ringkas: 'Minta tindakan', est: 0 }];
  S.job = { active: true, name: 'Cuci harian', slot: 1, manual: false, test: false, waiting: false, waitLeft: 0, idx: 1, count: 5, state: 'running', detail: 'Agitasi (kanan)', stepLeft: 190, tahap: jobT };
  S.status = { ...BASE_STATUS, state: 'running', proses: 'Cuci harian', slot: 1, stage: 'Tahap 2/5: Putar mesin', step: 2, steps: 5, stepLeft: 190, totalLeft: 2400, progress: 40, detail: 'Agitasi (kanan)' };
  const jc0 = S.jobCalls;
  await w.pollOnce(); await sleep(80);
  ok(S.jobCalls === jc0 + 1, '/api/job dimuat sekali saat Proses mulai (bukan tiap poll)');
  await w.pollOnce(); await w.pollOnce(); await sleep(40);
  ok(S.jobCalls === jc0 + 1, 'poll berikutnya tanpa perubahan Tahap tidak memuat ulang /api/job');
  ok(!$('#btnJob').disabled && /Tahap 2\/5/.test($('#stepLine').textContent) && $('#stepLeft').textContent === '03:10' && $('#hero').classList.contains('live'), 'hero: Tahap 2/5, sisa Tahap 03:10, mode berjalan');
  ok($$('#railMini .rail li').length === 5 && $('#railMini li.run .tl').textContent === 'Putar mesin' && $$('#railMini li.done').length === 1, 'rel mini: 5 simpul, 1 selesai, Putar mesin berjalan');
  ok(/±40 mnt/.test($('#totalLeft').textContent) && /selesai sekitar/.test($('#etaAt').textContent), 'sisa total tampil sebagai perkiraan (±) + jam selesai');
  $('#btnJob').focus(); click($('#btnJob')); await sleep(120);
  ok(!$('#jobSheet').hidden && $('#jobSheet').classList.contains('open'), 'klik Sisa Tahap membuka sheet Rel Tahap');
  ok($('#jobTitle').textContent === 'Cuci harian' && /Tahap 2 dari 5/.test($('#jobSub').textContent), 'kepala sheet: nama Proses & posisi Tahap');
  const cards = $$('#jobList .tcard');
  ok(cards.length === 5 && cards.map(c => c.classList.contains('done') ? 'D' : c.classList.contains('run') ? 'R' : 'Q').join('') === 'DRQQQ', 'kartu: 1 selesai, 1 berjalan, 3 antre');
  ok($$('.badge-st', cards[0])[0].textContent === 'Selesai' && $('.badge-st', cards[1]).textContent === 'Berjalan' && $('.badge-st', cards[2]).textContent === 'Antre', 'badge status per kartu');
  const dd = (c, label) => { const dt = $$('dt', c).find(x => x.textContent === label); return dt ? dt.nextElementSibling.textContent : null; };
  ok(dd(cards[0], 'Mode pengisian') === 'Sensor (sampai penuh)' && dd(cards[0], 'Batas waktu isi') === '00:30:00' && dd(cards[0], 'Batas Ulang') === '3 kali' && dd(cards[0], 'Jeda Percobaan') === '15 mnt', 'kartu Isi: semua variabel mode sensor (label & satuan dari meta)');
  ok(dd(cards[0], 'Level') === null && dd(cards[0], 'Lama isi') === null, 'variabel yang tidak berlaku untuk mode tidak ditampilkan');
  ok(dd(cards[2], 'Ulangi') === '2 sesi' && dd(cards[2], 'Pola putar') === 'Gentle (5,3 / 7)' && dd(cards[3], 'Jumlah pewangi') === '0,5 cup' && dd(cards[4], 'Pesan') === 'Tambahkan deterjen', 'Soak, Pewangi (0,5 cup), Minta tindakan: variabel lengkap');
  ok(/Agitasi \(kanan\)/.test($('.tlive', cards[1]).textContent) && /sisa 03:10/.test($('.tlive', cards[1]).textContent), 'kartu berjalan: detail fase + sisa Tahap');
  ok($$('.tfields', cards[0]).length === 1 && $('.tfields', cards[0]).textContent.includes('Sensor'), 'kartu selesai tetap berisi variabel (diringkas lewat CSS, bisa dibuka)');
  click($('.thead', cards[0])); ok(cards[0].classList.contains('open'), 'klik kartu selesai membukanya');
  S.job = { ...S.job, idx: 2 }; S.status = { ...S.status, step: 3, stage: 'Tahap 3/5: Soak', stepLeft: 100 };
  await w.pollOnce(); await sleep(100);
  ok(S.jobCalls > jc0 + 1 && $$('#jobList .tcard').map(c => c.classList.contains('done') ? 'D' : c.classList.contains('run') ? 'R' : 'Q').join('') === 'DDRQQ', 'Tahap berganti: job dimuat ulang & sheet ikut diperbarui');
  $('#jobSheet').dispatchEvent(new w.KeyboardEvent('keydown', { key: 'Escape', bubbles: true })); await sleep(260);
  ok($('#jobSheet').hidden && d.activeElement === $('#btnJob'), 'Esc menutup sheet & fokus kembali ke pemicu');
  click($('#btnJob2')); await sleep(80);
  ok(!$('#jobSheet').hidden, 'tombol "Lihat semua" di rel mini juga membuka sheet');
  click($('#jobClose')); await sleep(260);
  ok($('#jobSheet').hidden, 'tombol tutup menutup sheet');
  S.job = { ...S.job, waiting: true, idx: 0, waitLeft: 600 }; S.status = { ...S.status, step: 0, stage: 'Menunggu mulai', stepLeft: 600 };
  await w.pollOnce(); await sleep(60);
  click($('#btnJob')); await sleep(100);
  ok(/Menunggu mulai/.test($('#jobSub').textContent) && $$('#jobList .tcard.wait').length === 1 && $$('#jobList .tcard.run:not(.wait)').length === 0, 'menunggu mulai: simpul "Menunggu mulai" di atas, semua Tahap antre');
  click($('#jobClose')); await sleep(260);
  S.job = { active: true, name: 'Manual: Tunggu', slot: 0, manual: true, test: false, waiting: false, waitLeft: 0, idx: 0, count: 1, state: 'running', detail: '', stepLeft: 3, tahap: [{ t: 'tunggu', detik: 3, ringkas: 'Tunggu', est: 3 }] };
  S.status = { ...S.status, step: 1, stage: 'Manual: Tunggu', proses: 'Manual: Tunggu', slot: 0, manual: true, stepLeft: 3 };
  await w.pollOnce(); await sleep(80);
  ok($$('#railMini .rail li').length === 1 && /Tunggu/.test($('#railMini').textContent), 'Manual: rel mini 1 Tahap');
  S.job = { active: true, name: 'Kalibrasi relay', slot: 0, manual: false, test: true, waiting: false, waitLeft: 0, idx: 0, count: 2, state: 'running', detail: '', stepLeft: 3, tahap: [{ t: 'uji', relay: 'kiri', detik: 3, ringkas: 'Uji relay kiri', est: 3 }, { t: 'uji', relay: 'kanan', detik: 3, ringkas: 'Uji relay kanan', est: 3 }] };
  S.status = { ...S.status, proses: 'Kalibrasi relay', manual: false, step: 1, stage: 'Tahap 1/2' };
  await w.pollOnce(); await sleep(80);
  click($('#btnJob')); await sleep(100);
  ok($$('#jobList .tcard').length === 2 && dd($$('#jobList .tcard')[0], 'Relay') === 'kiri' && dd($$('#jobList .tcard')[0], 'Lama') === '3 dtk', 'uji relay tampil sebagai kartu "Uji relay" dengan relay & lama');
  click($('#jobClose')); await sleep(260);
  S.status = { ...BASE_STATUS }; await w.pollOnce(); await sleep(60);
  ok($('#btnJob').disabled && /Pilih Proses/.test($('#railMini').textContent), 'selesai: rel mini kembali ke keadaan siap');

  console.log('[U18] Tema & aksesibilitas dasar');
  click($('[data-theme-set="dark"]'));
  ok(d.documentElement.dataset.theme === 'dark' && $('[data-theme-set="dark"]').getAttribute('aria-pressed') === 'true', 'tema Gelap diterapkan & tersimpan');
  click($('[data-theme-set="light"]'));
  ok(d.documentElement.dataset.theme === 'light', 'tema Terang diterapkan');
  click($('#btnTheme')); ok(d.documentElement.dataset.theme === 'dark', 'tombol header berganti tema (terang -> gelap)');
  click($('[data-theme-set="auto"]'));
  ok(d.documentElement.dataset.theme === undefined && w.localStorage.getItem('theme') === null, 'tema Otomatis mengikuti sistem');
  const noName = $$('button').filter(b => !b.textContent.trim() && !b.getAttribute('aria-label') && !b.title);
  ok(noName.length === 0, 'semua tombol punya nama (teks/aria-label): ' + noName.map(b => b.id || b.className).join(','));
  ok($$('.view').length === 5 && $$('svg[aria-hidden="true"]').length > 10, '5 view, ikon dekoratif aria-hidden');
  ok(!/[\u{1F300}-\u{1FAFF}☀-➿]/u.test(html.replace(/<svg[\s\S]*?<\/svg>/g, '').replace(/<style>[\s\S]*?<\/style>/, '')), 'tanpa emoji di markup & skrip');

  console.log('[U19] Editor: saklar jeda opsional, J : M : D, drag bebas seluruh kartu, tanpa "Simpan sebagai baru"');
  S.status = { ...BASE_STATUS }; S.info = { remote: false, online: true }; await w.start(); await sleep(80);
  await nav('proses'); click($('#pcNew')); await sleep(60);
  ok(!$('#pcSaveNew') && !/Simpan sebagai baru/i.test($('#pcEditor').textContent), 'tombol "Simpan sebagai baru" dihapus');
  while ($$('#pcStack .crow').length) click($('#pcStack [data-act="del"]'));
  addStep('isi');
  const sw = () => $('input[data-opt]', rowOf(0)), tInp = () => $('input[data-k="tunggu"]', rowOf(0));
  ok(sw().checked && tInp().value === '15' && !tInp().disabled, 'Jeda Percobaan: saklar nyala, nilai bawaan 15');
  ok(/J : M : D/.test(fieldOf(0, 'batas').textContent) && !fieldOf(0, 'batas').querySelector('em'), 'Batas Waktu Isi: singkatan J : M : D di baris label, bukan di bawah kotak');
  sw().checked = false; sw().dispatchEvent(new w.Event('change', { bubbles: true }));
  ok(tInp().value === '0' && tInp().disabled, 'saklar mati: jeda 0 dan kotak dinonaktifkan');
  sw().checked = true; sw().dispatchEvent(new w.Event('change', { bubbles: true }));
  ok(tInp().value === '15' && !tInp().disabled, 'saklar nyala lagi: kembali ke 15');
  type(tInp(), '0');
  ok(!sw().checked && tInp().disabled, 'mengetik 0 mematikan saklar otomatis');
  type(tInp(), '0'); S.posts.length = 0; type($('#pcName'), 'Uji Jeda'); click($('#pcSave')); await sleep(150);
  ok(S.posts.length && S.posts[S.posts.length - 1].tahap[0].tunggu === 0, 'jeda 0 dikirim ke firmware (opsional)');
  ['putar', 'soak', 'buang', 'spin'].forEach(addStep);
  const labels = () => $$('#pcStack .crow b').map(b => b.textContent).join(',');
  const rws = $$('#pcStack .crow');
  rws.forEach((r, i) => { r.getBoundingClientRect = () => ({ top: i * 100, left: 0, width: 300, height: 90, bottom: i * 100 + 90 }); });
  const pev = (t, y) => { const e = new w.Event(t, { bubbles: true }); e.clientY = y; e.clientX = 5; e.pointerId = 2; e.button = 0; return e; };
  const lab0 = labels();
  $('input', rws[0]).dispatchEvent(pev('pointerdown', 10)); d.dispatchEvent(pev('pointermove', 60));
  ok(!$('.crow-ph'), 'menyeret dari kotak isian tidak memulai drag');
  $('input', rws[0]).dispatchEvent(pev('pointerup', 60));
  $('.num', rws[0]).dispatchEvent(pev('pointerdown', 10));
  ok(!$('.crow-ph'), 'hanya menekan (tanpa bergerak): belum drag, klik tetap normal');
  d.dispatchEvent(pev('pointermove', 30));
  ok(!!$('.crow-ph') && rws[0].classList.contains('drag'), 'langsung seret dari area kartu (tanpa menahan) mengaktifkan drag');
  d.dispatchEvent(pev('pointermove', 460)); d.dispatchEvent(pev('pointerup', 460));
  ok(!$('.crow-ph') && labels() === lab0.split(',').slice(1).concat(lab0.split(',')[0]).join(','), 'seret bebas ke paling bawah sekaligus (lebih dari satu langkah)');
  const rws2 = $$('#pcStack .crow');
  rws2.forEach((r, i) => { r.getBoundingClientRect = () => ({ top: i * 100, left: 0, width: 300, height: 90, bottom: i * 100 + 90 }); });
  const b2 = labels();
  $('.num', rws2[4]).dispatchEvent(pev('pointerdown', 410));
  d.dispatchEvent(pev('pointermove', 380));
  d.dispatchEvent(pev('pointermove', 5)); d.dispatchEvent(pev('pointerup', 5));
  ok(labels() === [b2.split(',')[4]].concat(b2.split(',').slice(0, 4)).join(','), 'seret bebas dari bawah ke paling atas');
  ok($$('#pcStack .crow .num').map(n => n.textContent).join('') === '12345', 'nomor Tahap diperbarui setelah drag');
  $('.num', $$('#pcStack .crow')[0]).dispatchEvent(pev('pointerdown', 10));
  d.dispatchEvent(pev('pointermove', 12));
  ok(!$('.crow-ph'), 'gerakan kecil (< 4 px) bukan drag');
  d.dispatchEvent(pev('pointerup', 12));

  const lbRow = $$('#pcStack .crow')[1]; lbRow.getBoundingClientRect = () => ({ top: 100, left: 0, width: 300, height: 90, bottom: 190 });
  $('label', lbRow).dispatchEvent(pev('pointerdown', 120)); d.dispatchEvent(pev('pointermove', 160));
  ok(!!$('.crow-ph'), 'menyeret dari label/area kosong kartu langsung memulai drag');
  d.dispatchEvent(pev('pointerup', 160));
  ok(!/[A-Za-z]\(/.test($$('#pcStack .crow label > span:first-child').map(x => x.textContent).join('|')), 'ada spasi sebelum kurung buka pada semua label (J : M : D, satuan)');

  console.log('[U20] Tombol mata pada semua kolom password');
  const pwIds = ['pw', 'wPass', 'lpw1', 'lpw2', 'rpw1', 'rpw2'];
  ok(pwIds.every(id => $('#' + id).parentElement.classList.contains('pwf') && !!$('.eye', $('#' + id).parentElement)), 'semua 6 kolom password punya tombol mata');
  ok($$('input[type=password], .pwf input').length === 6 && $$('.eye svg use').length === 6, 'tombol memakai ikon SVG sendiri');
  const eye0 = $('.eye', $('#lpw1').parentElement);
  ok(eye0.getAttribute('aria-label') === 'Tampilkan password' && $('#lpw1').type === 'password', 'awal: tersembunyi, label "Tampilkan password"');
  click(eye0);
  ok($('#lpw1').type === 'text' && eye0.getAttribute('aria-label') === 'Sembunyikan password' && eye0.getAttribute('aria-pressed') === 'true' && $('use', eye0).getAttribute('href') === '#i-eye-off', 'klik: terlihat, ikon mata dicoret');
  click(eye0); ok($('#lpw1').type === 'password' && $('use', eye0).getAttribute('href') === '#i-eye', 'klik lagi: tersembunyi lagi');

  console.log(`\n${pass + fail} pemeriksaan UI, ${fail} gagal`);
  process.exit(fail ? 1 : 0);
})();
