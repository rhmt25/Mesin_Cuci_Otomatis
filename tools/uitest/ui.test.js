const { JSDOM } = require('jsdom');
const fs = require('fs');
const root = require('path').resolve(__dirname, '../../web') + '/';
let html = fs.readFileSync(root + 'index.html', 'utf8')
  .replace('/*CSS*/', fs.readFileSync(root + 'style.css', 'utf8'))
  .replace('/*JS*/', '');
const js = fs.readFileSync(root + 'app.js', 'utf8');

let pass = 0, fail = 0;
const ok = (c, m) => { if (c) { pass++; console.log('  ok:', m); } else { fail++; console.log('  GAGAL:', m); } };
const sleep = ms => new Promise(r => setTimeout(r, ms));

// ------------------------------------------------------------ server palsu ----
const S = {
  status: { type: 'status', state: 'idle', program: '', stage: '', detail: '', pause: '', step: 0, steps: 0, stepLeft: 0, totalLeft: 0, progress: 0,
    water: 'kosong', waterRaw: false, fault: false, resume: { pending: false, info: '' }, relays: { kiri: false, kanan: false, isi: false, buang: false, spin: false, pewangi: false },
    wifi: { online: true, ssid: 'x', rssi: -50, ip: '1.2.3.4', ap: 'MesinCuci-1', apClients: 0, time: '10:00' }, notif: 'Lengkap', uptime: 100, heap: 100000, fw: '2.0.0', reset: 'x' },
  settings: { pola: 0, soak: 2, wash: 15, rinse: 10, parfume: 1, spin: 10, fillLevel: 0, fillPercent: 50, fillTime: 30, flH: 0, flM: 30, flS: 0, fillRetryCount: 3, fillRetryWait: 15, drainMode: 0, drainTime: 5, drainSessionLimit: 3, drainProbeSec: 45, fillPrecheck: 0, unjamCount: 2, dH: 0, dM: 0, dS: 0, finishAt: -1, notif: 0, calib: { fillSec: 600, nFill: 2 }, timeSynced: true },
  slots: [null, null, null, null, null],
  devices: [{ id: 0, name: 'Chrome - Android', created: 1760000000, lastSeen: 1760000500, current: false }, { id: 1, name: 'Edge - Windows', created: 1760100000, lastSeen: 1760100500, current: true }],
  cmds: [], posts: [],
};
function route(url, opt) {
  const m = (opt && opt.method) || 'GET', u = new URL(url, 'http://x/'), p = u.pathname;
  const body = opt && opt.body ? JSON.parse(opt.body) : null;
  const json = (o, code = 200) => Promise.resolve({ status: code, json: () => Promise.resolve(o) });
  if (p === '/api/status') return json(S.status);
  if (p === '/api/logs') return json({ entries: [], last: 0 });
  if (p === '/api/settings') return json(S.settings);
  if (p === '/api/cmd') { S.cmds.push(body); return json({ ok: !(body.cmd === 'relay' && body.args === 'kanan on'), msg: body.cmd === 'relay' ? 'Ditolak: motor kiri sedang aktif' : 'ok' }); }
  if (p === '/api/custom' && m === 'GET') return json({ max: 5, maxItems: 24, slots: S.slots });
  if (p === '/api/custom' && m === 'POST') {
    S.posts.push(body);
    let slot = body.slot || (S.slots.findIndex(x => !x) + 1);
    S.slots[slot - 1] = { slot, name: body.name, steps: 0, items: body.items };
    return json({ ok: true, slot, msg: 'Tersimpan di slot ' + slot });
  }
  if (p === '/api/custom' && m === 'DELETE') { S.slots[+u.searchParams.get('slot') - 1] = null; return json({ ok: true, msg: 'Proses dihapus' }); }
  if (p === '/api/devices' && m === 'GET') return json({ devices: S.devices, max: 5, temporary: false });
  if (p === '/api/devices' && m === 'DELETE') {
    if (u.searchParams.get('others')) { S.devices = S.devices.filter(d => d.current); return json({ ok: true, msg: '1 perangkat lain dihapus' }); }
    const id = +u.searchParams.get('id'); const d = S.devices.find(x => x.id === id); S.devices = S.devices.filter(x => x.id !== id);
    return json({ ok: true, self: !!(d && d.current), msg: 'Perangkat dihapus' });
  }
  return json({ ok: false, msg: 'tidak ada' }, 404);
}

(async () => {
  let wsInst = null;
  const dom = new JSDOM(html, {
    runScripts: 'outside-only', pretendToBeVisual: true, url: 'http://localhost/',
    beforeParse(w) {
      w.fetch = (u, o) => route(u, o);
      w.WebSocket = class { constructor() { wsInst = this; setTimeout(() => this.onopen && this.onopen(), 0); } close() { } };
      w.confirm = () => true;
      w.matchMedia = () => ({ matches: false });
      w.Element.prototype.setPointerCapture = () => { };
      w.Element.prototype.scrollIntoView = () => { };
      w.scrollBy = () => { };
    },
  });
  const w = dom.window, d = w.document;
  w.eval(js);
  await sleep(150);
  const $ = s => d.querySelector(s), $$ = s => [...d.querySelectorAll(s)];
  const click = el => el.dispatchEvent(new w.MouseEvent('click', { bubbles: true }));
  const type = (el, v) => { el.value = v; el.dispatchEvent(new w.Event('input', { bubbles: true })); };

  console.log('[U1] Start & login');
  ok(!$('#app').hidden && $('#login').hidden, 'dashboard tampil setelah status OK');
  ok($('#remember').checked, 'centang ingat perangkat aktif secara default');

  console.log('[U2] Custom: susun, ubah nilai, tukar urutan, simpan');
  click($('[data-tab="custom"]')); await sleep(100);
  click($('[data-add="rinse"]')); click($('[data-add="rinse"]'));
  ok($$('#cpStack .crow').length === 2, 'dua langkah Bilas ditambahkan');
  const inputs = $$('#cpStack input[data-k="a"]');
  type(inputs[0], '10'); type(inputs[1], '5');
  ok($('#cpSummary').textContent.includes('2 langkah') && $('#cpSummary').textContent.includes('6 langkah mesin'), 'ringkasan: 2 langkah / 6 langkah mesin');
  click($$('#cpStack [data-act="up"]')[1]);   // bilas 5 naik
  const order = $$('#cpStack input[data-k="a"]').map(i => i.value);
  ok(order.join(',') === '5,10', 'tombol naik menukar urutan (5 lalu 10)');
  click($('[data-add="spin"]'));
  click($$('#cpStack [data-act="dup"]')[2]);
  ok($$('#cpStack .crow').length === 4, 'duplikat bekerja');
  click($$('#cpStack [data-act="del"]')[3]);
  ok($$('#cpStack .crow').length === 3, 'hapus bekerja');
  // validasi
  click($('#cpSave')); await sleep(50);
  ok(!$('#cpError').hidden && /Nama/.test($('#cpError').textContent), 'nama kosong ditolak dengan pesan');
  $('#cpName').value = 'Bilas dua kali';
  type($$('#cpStack input[data-k="a"]')[2], '0');
  click($('#cpSave')); await sleep(50);
  ok(!$('#cpError').hidden && /Langkah 3/.test($('#cpError').textContent), 'nilai 0 ditolak menyebut nomor langkah');
  type($$('#cpStack input[data-k="a"]')[2], '2');
  click($('#cpSave')); await sleep(150);
  const post = S.posts[S.posts.length - 1];
  ok(post && post.name === 'Bilas dua kali' && post.items.map(i => i.t + ':' + i.a).join(',') === 'rinse:5,rinse:10,spin:2', 'payload simpan: urutan & nilai benar');
  ok($$('#cpList .cpcard').length === 1 && $('#cpList').textContent.includes('Bilas dua kali'), 'daftar tersimpan menampilkan proses');
  ok(!$('#homeCustom').hidden && $('#homeCustomGrid').textContent.includes('Bilas dua kali'), 'tampil juga di Beranda');
  click($('[data-run]')); await sleep(50);
  ok(S.cmds.some(c => c.cmd === 'custom' && c.args === '1'), 'tombol Jalankan mengirim custom 1');

  console.log('[U3] Contoh/preset & edit');
  const sel = $('#cpPreset'); sel.value = 'cepat'; sel.dispatchEvent(new w.Event('change'));
  ok($$('#cpStack .crow').length === 3 || $$('#cpStack .crow').length === 4, 'contoh Cepat memuat langkah');
  click($('[data-edit]')); await sleep(50);
  ok($('#cpName').value === 'Bilas dua kali' && $$('#cpStack .crow').length === 3, 'Edit memuat proses tersimpan');

  console.log('[U4] Drag & drop (pointer) menukar urutan');
  const rows = $$('#cpStack .crow');
  rows.forEach((r, i) => { r.getBoundingClientRect = () => ({ top: i * 100, height: 90, bottom: i * 100 + 90 }); });
  const h0 = rows[0].querySelector('.handle');
  const ev = (t, y) => { const e = new w.Event(t, { bubbles: true }); e.clientY = y; e.pointerId = 1; return e; };
  h0.dispatchEvent(ev('pointerdown', 10));
  h0.dispatchEvent(ev('pointermove', 250));   // di bawah baris ke-3
  h0.dispatchEvent(ev('pointerup', 250));
  const after = $$('#cpStack input[data-k="a"]').map(i => i.value).join(',');
  ok(after === '10,2,5', 'seret baris 1 ke bawah: urutan menjadi 10,2,5 (sekarang ' + after + ')');

  console.log('[U5] Relay: dikunci saat proses berjalan & konflik');
  click($('[data-tab="relay"]')); await sleep(50);
  const onBtn = k => $(`[data-rl="${k}"][data-on="1"]`);
  ok(!onBtn('kiri').disabled && !$('#btnSeq').disabled, 'saat siap: ON kiri & uji berurutan aktif');
  S.status = { ...S.status, relays: { ...S.status.relays, kiri: true } };
  wsInst.onmessage({ data: JSON.stringify(S.status) });
  ok(onBtn('kanan').disabled && /Motor kiri sedang aktif/.test($('#hint-kanan').textContent), 'kiri ON -> tombol ON kanan terkunci + petunjuk jelas');
  ok(!$(`[data-rl="kiri"][data-on="0"]`).disabled, 'OFF kiri aktif');
  S.status = { ...S.status, state: 'running', program: 'Normal', stage: 'Bilas 2/3', relays: { ...S.status.relays, kiri: false } };
  wsInst.onmessage({ data: JSON.stringify(S.status) });
  ok($$('[data-rl]').every(b => b.disabled) && $('#btnSeq').disabled, 'proses berjalan: semua ON/OFF & uji berurutan terkunci');
  ok(!$('#relayLock').hidden && $('#relayLock').textContent.includes('Normal'), 'banner kunci menyebut proses yang berjalan');
  ok($$('.prog').every(b => b.disabled) && !$('#progLock').hidden, 'kartu program di Beranda juga terkunci');
  S.status = { ...S.status, state: 'idle', program: '' };
  wsInst.onmessage({ data: JSON.stringify(S.status) });
  ok(!onBtn('isi').disabled && $('#relayLock').hidden, 'kembali siap: terbuka lagi');

  console.log('[U6] Pesan penolakan');
  onBtn('kanan').disabled = false; click(onBtn('kanan')); await sleep(80);
  ok(/Ditolak: motor kiri sedang aktif/.test($('#toast').textContent), 'toast menampilkan alasan penolakan dari server');
  wsInst.onmessage({ data: JSON.stringify({ type: 'log', last: 5, entries: [{ s: 5, t: 1, e: 0, l: 4, m: 'Interlock menolak relay kanan' }] }) });
  ok(/Interlock menolak relay kanan/.test($('#toast').textContent), 'log ERROR dari mesin muncul sebagai toast');

  console.log('[U7] Perangkat diingat');
  click($('[data-tab="system"]')); await sleep(100);
  ok($$('#devList .wifirow').length === 2 && $('#devList').textContent.includes('Perangkat ini'), 'daftar 2 perangkat, penanda "Perangkat ini"');
  click($('#devList [data-dev="0"]')); await sleep(100);
  ok($$('#devList .wifirow').length === 1, 'hapus perangkat lain');
  click($('#devList [data-dev="1"]')); await sleep(100);
  ok(!$('#login').hidden, 'menghapus perangkat sendiri -> kembali ke login');

  console.log('[U8] Tab Air: probe tarikan & pra-cek isi');
  d.querySelector('#login').hidden = true; d.querySelector('#app').hidden = false;
  click($('[data-tab="water"]')); await sleep(120);
  const probe = $('input[data-key="drainProbeSec"]');
  ok(probe && probe.value === '45', 'field probe tarikan (default 45 dtk) tampil');
  ok(!$('input[data-key="drainExtraSec"]') && !$('input[data-key="drainMaxMin"]'), 'field tambahan/maks buang lama sudah hilang');
  ok($('#wPrecheck').value === 'buang', 'pra-cek isi default = buang');
  $('#wPrecheck').value = 'jeda'; click($('#btnPrecheck')); await sleep(60);
  ok(S.cmds.some(c => c.cmd === 'pra_cek_isi' && c.args === 'jeda'), 'pilihan pra-cek dikirim sebagai pra_cek_isi jeda');
  type(probe, '30'); click($('[data-set="set_probe_buang"]')); await sleep(60);
  ok(S.cmds.some(c => c.cmd === 'set_probe_buang' && c.args === '30'), 'probe diubah lewat set_probe_buang');
  ok(/penuh|tidak penuh/.test($('#tab-water').textContent), 'teks bantuan sensor di atas tampil');

  console.log(`\n${pass + fail} pemeriksaan UI, ${fail} gagal`);
  process.exit(fail ? 1 : 0);
})();
