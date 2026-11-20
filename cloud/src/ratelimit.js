// Batas laju login (murni, bisa diuji). Penguncian bertingkat per IP + batas global untuk serangan dari banyak IP.
export const FAILS_PER_LOCK = 5;
export const LEVELS_MS = [5 * 60e3, 15 * 60e3, 60 * 60e3, 6 * 3600e3, 24 * 3600e3];   // lama kunci tiap tingkat
export const DECAY_MS = 24 * 3600e3;                                                   // tanpa percobaan gagal selama ini: tingkat turun ke 0
export const GLOBAL = { windowMs: 10 * 60e3, max: 50, lockMs: 5 * 60e3 };             // >50 gagal/10 menit dari semua IP: semua dikunci 5 menit

export function applyFail(rec, now) {
  if (rec.level > 0 && rec.lastFail && now - Math.max(rec.lastFail, rec.lockUntil || 0) > DECAY_MS) { rec.level = 0; rec.n = 0; }
  rec.lastFail = now;
  rec.n = (rec.n || 0) + 1;
  if (rec.n >= FAILS_PER_LOCK) {
    rec.lockUntil = now + LEVELS_MS[Math.min(rec.level || 0, LEVELS_MS.length - 1)];
    rec.level = (rec.level || 0) + 1;
    rec.n = 0;
  }
  return rec;
}

export function applyGlobalFail(g, now) {
  if (!g.start || now - g.start > GLOBAL.windowMs) { g.start = now; g.count = 0; }
  g.count = (g.count || 0) + 1;
  if (g.count >= GLOBAL.max) { g.lockUntil = now + GLOBAL.lockMs; g.count = 0; g.start = now; }
  return g;
}
