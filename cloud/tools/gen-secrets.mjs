// Membuat rahasia untuk Worker + ESP32, disimpan LOKAL di .secrets.local.json (tidak masuk git).
//   PowerShell:  $env:REMOTE_PASSWORD="password-web-jarak-jauh"; node tools/gen-secrets.mjs
//   Putar token perangkat & sesi: tambahkan --rotate
import { randomBytes } from "node:crypto";
import fs from "node:fs";
import { makePasswordHash } from "../src/auth.js";

const file = new URL("../.secrets.local.json", import.meta.url);
const rotate = process.argv.includes("--rotate");
const password = process.env.REMOTE_PASSWORD;
let cur = {};
try { cur = JSON.parse(fs.readFileSync(file, "utf8")); } catch { /* belum ada */ }

if (!password && !cur.REMOTE_PASSWORD_HASH) {
  console.error("Isi dulu password: $env:REMOTE_PASSWORD=\"...\" (min. 10 karakter) lalu jalankan lagi.");
  process.exit(1);
}
if (password && password.length < 10) {
  console.error("Password jarak jauh minimal 10 karakter.");
  process.exit(1);
}
const out = {
  DEVICE_TOKEN: !rotate && cur.DEVICE_TOKEN ? cur.DEVICE_TOKEN : randomBytes(32).toString("hex"),
  SESSION_SECRET: !rotate && cur.SESSION_SECRET ? cur.SESSION_SECRET : randomBytes(32).toString("hex"),
  REMOTE_PASSWORD_HASH: password ? await makePasswordHash(password) : cur.REMOTE_PASSWORD_HASH,
};
fs.writeFileSync(file, JSON.stringify(out, null, 2), { mode: 0o600 });
console.log("Rahasia tersimpan di cloud/.secrets.local.json (jangan di-commit, jangan dibagikan).");
console.log(rotate ? "Token perangkat & sesi DIPUTAR: perbarui REMOTE_DEVICE_TOKEN di secrets.h ESP32 dan jalankan put-secrets lagi." : "Berikutnya: node tools/put-secrets.mjs");
