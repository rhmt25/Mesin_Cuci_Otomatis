// Mengirim rahasia ke Cloudflare (wrangler secret put) tanpa menampilkannya.
// Token & chat id Telegram diambil otomatis dari firmware/mesin_cuci_otomatis/secrets.h (lokal).
import { spawnSync } from "node:child_process";
import fs from "node:fs";

const local = JSON.parse(fs.readFileSync(new URL("../.secrets.local.json", import.meta.url), "utf8"));
const espSecrets = new URL("../../firmware/mesin_cuci_otomatis/secrets.h", import.meta.url);
const grab = (name) => {
  try {
    const m = fs.readFileSync(espSecrets, "utf8").match(new RegExp(String.raw`#define\s+${name}\s+"([^"]*)"`));
    return m ? m[1] : "";
  } catch { return ""; }
};
const secrets = {
  DEVICE_TOKEN: local.DEVICE_TOKEN,
  SESSION_SECRET: local.SESSION_SECRET,
  REMOTE_PASSWORD_HASH: local.REMOTE_PASSWORD_HASH,
  TELEGRAM_BOT_TOKEN: process.env.TELEGRAM_BOT_TOKEN || grab("TELEGRAM_BOT_TOKEN"),
  TELEGRAM_CHAT_ID: process.env.TELEGRAM_CHAT_ID || grab("TELEGRAM_CHAT_ID"),
};
for (const [name, value] of Object.entries(secrets)) {
  if (!value || value.startsWith("ISI_")) { console.error(`Lewati ${name}: nilai kosong`); continue; }
  const r = spawnSync("npx", ["wrangler", "secret", "put", name], { input: value, shell: true, encoding: "utf8" });
  console.log(`${name}: ${r.status === 0 ? "terkirim" : "GAGAL (" + (r.stderr || r.stdout || "").trim().slice(0, 200) + ")"}`);
}
