#include "proses_store.h"
#include "logger.h"

#define PROSES_MAX_FILE 8192   // batas ukuran berkas yang dibaca (Proses terbesar ±3 KB)

// Serialisasi/parse di satu tempat agar jalur perangkat dan uji PC identik.
static bool encode(const Proses& p, String& out) {
  JsonDocument d;
  prosesToJson(p, d.to<JsonObject>());
  out = "";
  serializeJson(d, out);
  return out.length() > 0 && out.length() < PROSES_MAX_FILE;
}

static bool decode(const String& raw, Proses& out) {
  if (raw.length() == 0 || raw.length() >= PROSES_MAX_FILE) return false;
  JsonDocument d;
  if (deserializeJson(d, raw.c_str()) != DeserializationError::Ok) return false;
  String err;
  return prosesFromJson(d.as<JsonVariantConst>(), out, err, true);
}

#ifdef HOST_TEST
// ------------------------------------------------------------ penyimpanan di memori (uji PC)
static String files[PROSES_SLOTS];
static bool present[PROSES_SLOTS];
static bool failWrites = false;

bool prosesStoreBegin() { return true; }
bool prosesStoreReady() { return true; }
static bool readRaw(uint8_t slot, String& raw) {
  if (slot >= PROSES_SLOTS || !present[slot]) return false;
  raw = files[slot];
  return true;
}
static bool writeRaw(uint8_t slot, const String& raw) {
  if (slot >= PROSES_SLOTS || failWrites) return false;
  files[slot] = raw;
  present[slot] = true;
  return true;
}
static bool removeRaw(uint8_t slot) {
  if (slot >= PROSES_SLOTS) return false;
  present[slot] = false;
  files[slot] = "";
  return true;
}
void prosesStoreHostReset() {
  for (int i = 0; i < PROSES_SLOTS; i++) removeRaw(i);
  failWrites = false;
}
void prosesStoreHostPut(uint8_t slot, const String& raw) { writeRaw(slot, raw); }
String prosesStoreHostGet(uint8_t slot) { return slot < PROSES_SLOTS ? files[slot] : String(""); }
void prosesStoreHostFailWrites(bool fail) { failWrites = fail; }
#else
// ------------------------------------------------------------ LittleFS (perangkat)
#include <LittleFS.h>
#include <esp_log.h>

static bool mounted = false;
static uint16_t presentMask = 0;   // bit N = berkas pN.json ada (hindari open() pada berkas tak ada: menulis galat palsu ke serial)

static String pathOf(uint8_t slot, const char* ext) { return String("/p") + String((int)slot) + ext; }

bool prosesStoreBegin() {
  // formatOnFail=true aman: partisi ini tidak dipakai data lain; hanya terformat bila belum pernah dipasang.
  mounted = LittleFS.begin(true, "/lfs", 6, "spiffs");
  if (!mounted) {
    logf(LOG_ERROR, "Penyimpanan Proses (LittleFS) gagal dipasang");
    return false;
  }
  presentMask = 0;
  File root = LittleFS.open("/");
  for (File f = root.openNextFile(); f; f = root.openNextFile()) {   // daftar berkas yang ada
    String name = f.name();
    if (name.startsWith("/")) name.remove(0, 1);
    f.close();
    if (name.length() >= 6 && name[0] == 'p' && name[1] >= '0' && name[1] <= '9') {
      uint8_t slot = (uint8_t)(name[1] - '0');
      if (name.endsWith(".json")) presentMask |= (1u << slot);
      else if (name.endsWith(".tmp")) LittleFS.remove(String("/") + name);   // sisa tulis yang terputus (listrik padam)
    }
  }
  return true;
}
bool prosesStoreReady() { return mounted; }

static bool readRaw(uint8_t slot, String& raw) {
  if (!mounted || slot >= PROSES_SLOTS) return false;
  if (!(presentMask & (1u << slot))) return false;
  String path = pathOf(slot, ".json");
  File f = LittleFS.open(path, "r");
  if (!f) return false;
  size_t n = f.size();
  if (n == 0 || n >= PROSES_MAX_FILE) {
    f.close();
    raw = "";
    return true;   // ada tetapi tidak valid -> dianggap rusak oleh decode()
  }
  raw = "";
  raw.reserve(n);
  while (f.available()) raw += (char)f.read();
  f.close();
  return true;
}
static bool writeRaw(uint8_t slot, const String& raw) {
  if (!mounted || slot >= PROSES_SLOTS) return false;
  String tmp = pathOf(slot, ".tmp"), dst = pathOf(slot, ".json");
  File f = LittleFS.open(tmp, "w");
  if (!f) return false;
  size_t w = f.write((const uint8_t*)raw.c_str(), raw.length());
  f.close();
  if (w != raw.length()) {
    LittleFS.remove(tmp);
    return false;
  }
  if (!LittleFS.rename(tmp, dst)) {   // rename menimpa berkas lama; bila ditolak, hapus lalu ganti nama
    LittleFS.remove(dst);
    if (!LittleFS.rename(tmp, dst)) {
      LittleFS.remove(tmp);
      return false;
    }
  }
  presentMask |= (1u << slot);
  return true;
}
static bool removeRaw(uint8_t slot) {
  if (!mounted || slot >= PROSES_SLOTS) return false;
  if (!(presentMask & (1u << slot))) return true;
  presentMask &= ~(1u << slot);
  return LittleFS.remove(pathOf(slot, ".json"));
}
#endif

SlotState prosesSlotState(uint8_t slot) {
  String raw;
  if (!readRaw(slot, raw)) return SLOT_EMPTY;
  Proses* p = new Proses;
  if (!p) return SLOT_BAD;
  bool ok = decode(raw, *p);
  delete p;
  return ok ? SLOT_OK : SLOT_BAD;
}

bool prosesLoad(uint8_t slot, Proses& out) {
  String raw;
  if (!readRaw(slot, raw)) return false;
  return decode(raw, out);
}

bool prosesSave(uint8_t slot, const Proses& p) {
  String raw;
  if (!encode(p, raw)) return false;
  return writeRaw(slot, raw);
}

bool prosesDelete(uint8_t slot) { return removeRaw(slot); }
