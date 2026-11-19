// Notifikasi Telegram dikirim dari Worker (token tidak ada di ESP32).
export async function sendTelegram(env, text) {
  if (!env.TELEGRAM_BOT_TOKEN || !env.TELEGRAM_CHAT_ID) return false;
  const base = env.TELEGRAM_API_BASE || "https://api.telegram.org";
  const url = `${base}/bot${env.TELEGRAM_BOT_TOKEN}/sendMessage`;
  const body = JSON.stringify({ chat_id: env.TELEGRAM_CHAT_ID, text: String(text).slice(0, 4000) });
  for (let attempt = 0; attempt < 2; attempt++) {
    try {
      const r = await fetch(url, { method: "POST", headers: { "content-type": "application/json" }, body });
      if (r.ok) return true;
    } catch {
      /* coba lagi sekali */
    }
  }
  return false;
}

export function wibTime(ms) {
  return new Date(ms).toLocaleTimeString("id-ID", { timeZone: "Asia/Jakarta", hour: "2-digit", minute: "2-digit" });
}
