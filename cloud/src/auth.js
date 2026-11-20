// Autentikasi jarak jauh: hash password PBKDF2, sesi bertanda tangan HMAC (tanpa penyimpanan di server).
const enc = new TextEncoder();

export const PBKDF2_ITERATIONS = 100000; // batas maksimum PBKDF2 di Cloudflare Workers

export function toB64(buf) {
  let s = "";
  const bytes = new Uint8Array(buf);
  for (let i = 0; i < bytes.length; i++) s += String.fromCharCode(bytes[i]);
  return btoa(s);
}
export function fromB64(b64) {
  const s = atob(b64);
  const out = new Uint8Array(s.length);
  for (let i = 0; i < s.length; i++) out[i] = s.charCodeAt(i);
  return out;
}
const b64u = (buf) => toB64(buf).replace(/\+/g, "-").replace(/\//g, "_").replace(/=+$/, "");

export function timingSafeEqual(a, b) {
  const x = enc.encode(String(a));
  const y = enc.encode(String(b));
  let diff = x.length ^ y.length;
  const n = Math.max(x.length, y.length);
  for (let i = 0; i < n; i++) diff |= (x[i] || 0) ^ (y[i] || 0);
  return diff === 0;
}

async function pbkdf2(password, salt, iterations) {
  const key = await crypto.subtle.importKey("raw", enc.encode(password), "PBKDF2", false, ["deriveBits"]);
  return crypto.subtle.deriveBits({ name: "PBKDF2", hash: "SHA-256", salt, iterations }, key, 256);
}

// Format tersimpan: pbkdf2$<iterasi>$<salt b64>$<hash b64>
export async function makePasswordHash(password, iterations = PBKDF2_ITERATIONS) {
  const salt = crypto.getRandomValues(new Uint8Array(16));
  const bits = await pbkdf2(password, salt, iterations);
  return `pbkdf2$${iterations}$${toB64(salt)}$${toB64(bits)}`;
}

export async function verifyPassword(password, stored) {
  try {
    const [scheme, it, saltB64, hashB64] = String(stored).split("$");
    if (scheme !== "pbkdf2") return false;
    const bits = await pbkdf2(password, fromB64(saltB64), Number(it));
    return timingSafeEqual(toB64(bits), hashB64);
  } catch {
    return false;
  }
}

async function hmacKey(secret) {
  return crypto.subtle.importKey("raw", enc.encode(secret), { name: "HMAC", hash: "SHA-256" }, false, ["sign"]);
}
export async function hmac(secret, data) {
  return b64u(await crypto.subtle.sign("HMAC", await hmacKey(secret), enc.encode(data)));
}

// Token sesi: <kedaluwarsa (detik)>.<acak>.<epoch>.<tanda tangan>. Epoch naik tiap password diganti: sesi lama langsung tidak berlaku.
export async function makeSession(secret, ttlSec, epoch = 0) {
  const exp = Math.floor(Date.now() / 1000) + ttlSec;
  const rnd = b64u(crypto.getRandomValues(new Uint8Array(12)));
  const body = `${exp}.${rnd}.${epoch}`;
  return `${body}.${await hmac(secret, body)}`;
}

// Mengembalikan epoch sesi (>= 0) bila tanda tangan & masa berlaku sah, atau -1.
export async function verifySession(secret, token) {
  if (!token || !secret) return -1;
  const parts = String(token).split(".");
  if (parts.length !== 4) return -1;
  const body = `${parts[0]}.${parts[1]}.${parts[2]}`;
  if (!timingSafeEqual(await hmac(secret, body), parts[3])) return -1;
  if (!(Number(parts[0]) > Math.floor(Date.now() / 1000))) return -1;
  const ep = Number(parts[2]);
  return Number.isInteger(ep) && ep >= 0 ? ep : -1;
}

export function parseCookie(header, name) {
  if (!header) return "";
  for (const part of header.split(";")) {
    const i = part.indexOf("=");
    if (i > 0 && part.slice(0, i).trim() === name) return part.slice(i + 1).trim();
  }
  return "";
}
