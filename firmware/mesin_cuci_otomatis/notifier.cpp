#include "notifier.h"
#include "config.h"
#include "logger.h"
#include "settings.h"

struct Pending {
  uint8_t level;
  char text[LOG_TEXT_LEN];
};

static Pending queue[NOTIF_OFFLINE_MAX];
static int queueN = 0;
static uint32_t lastSeq = 0;
static uint32_t firstPendingMs = 0;
static bool online = false;
static bool wasOffline = false;
static uint32_t offlineSinceMs = 0;
static bool hadOutage = false;   // ada pesan tertahan saat offline

const char* notifModeName(uint8_t mode) {
  static const char* n[] = {"Lengkap", "Ringkas", "Senyap"};
  return mode < 3 ? n[mode] : "?";
}

static bool passes(uint8_t level) {
  switch (S.notif) {
    case NOTIF_RINGKAS: return level >= LOG_STEP;
    case NOTIF_SENYAP: return level >= LOG_ERROR;
    default: return level >= LOG_INFO;   // Lengkap: DEBUG hanya di log web
  }
}

void notifierInit() { lastSeq = logLastSeq(); }

static void enqueue(uint8_t level, const char* text) {
  if (queueN >= NOTIF_OFFLINE_MAX) {
    // buang entri paling tidak penting dulu (yang tertua)
    int drop = 0;
    for (int i = 0; i < queueN; i++) {
      if (queue[i].level < queue[drop].level) drop = i;
    }
    for (int i = drop; i < queueN - 1; i++) queue[i] = queue[i + 1];
    queueN--;
  }
  if (queueN == 0) firstPendingMs = millis();
  queue[queueN].level = level;
  strlcpy(queue[queueN].text, text, sizeof(queue[queueN].text));
  queueN++;
}

void notifierPoll() {
  LogEntry batch[16];
  for (;;) {
    size_t n = logRead(lastSeq, batch, 16);
    if (n == 0) break;
    for (size_t i = 0; i < n; i++) {
      lastSeq = batch[i].seq;
      if (passes(batch[i].level)) enqueue(batch[i].level, batch[i].text);
    }
    if (n < 16) break;
  }
}

void notifierSetOnline(bool isOnline) {
  if (isOnline == online) return;
  online = isOnline;
  if (!online) {
    wasOffline = true;
    offlineSinceMs = millis();
  } else if (wasOffline) {
    hadOutage = queueN > 0;
  }
}

bool notifierNextMessage(String& out) {
  if (!online || queueN == 0) return false;
  bool urgent = false;
  for (int i = 0; i < queueN; i++)
    if (queue[i].level >= LOG_ERROR) urgent = true;
  if (!urgent && millis() - firstPendingMs < NOTIF_BATCH_MS) return false;

  out = "";
  if (hadOutage) {
    uint32_t mins = (millis() - offlineSinceMs) / 60000;
    out += "\xF0\x9F\x93\xB4 Selama offline";
    if (mins > 0) out += " " + String(mins) + " menit";
    out += ":\n";
    hadOutage = false;
    wasOffline = false;
  }
  int used = 0;
  while (used < queueN) {
    String line = String(logLevelEmoji(queue[used].level)) + queue[used].text + "\n";
    if (out.length() + line.length() > 3500 && used > 0) break;
    out += line;
    used++;
  }
  for (int i = used; i < queueN; i++) queue[i - used] = queue[i];
  queueN -= used;
  firstPendingMs = millis();
  out.trim();
  return true;
}
