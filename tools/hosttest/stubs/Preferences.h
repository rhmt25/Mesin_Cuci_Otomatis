// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rahmat Hidayat
#pragma once
#include "Arduino.h"
// Penyimpanan NVS in-memory untuk uji.
class Preferences {
  std::string ns;
 public:
  static std::map<std::string, std::string>& db();
  bool begin(const char* n, bool = false) { ns = n; return true; }
  void end() {}
  bool isKey(const char* k) { return db().count(ns + "/" + k) > 0; }
  void clear() {
    std::string pre = ns + "/";
    for (auto it = db().begin(); it != db().end();) it = it->first.rfind(pre, 0) == 0 ? db().erase(it) : std::next(it);
  }
  void remove(const char* k) { db().erase(ns + "/" + k); }
  template <class T> void put(const char* k, T v) { db()[ns + "/" + k] = std::string((const char*)&v, sizeof(T)); }
  template <class T> T get(const char* k, T def) {
    auto it = db().find(ns + "/" + k);
    if (it == db().end() || it->second.size() != sizeof(T)) return def;
    T v; memcpy(&v, it->second.data(), sizeof(T)); return v;
  }
  void putUChar(const char* k, uint8_t v) { put(k, v); }
  uint8_t getUChar(const char* k, uint8_t d) { return get<uint8_t>(k, d); }
  void putInt(const char* k, int v) { put(k, v); }
  int getInt(const char* k, int d) { return get<int>(k, d); }
  void putFloat(const char* k, float v) { put(k, v); }
  float getFloat(const char* k, float d) { return get<float>(k, d); }
  void putUShort(const char* k, uint16_t v) { put(k, v); }
  uint16_t getUShort(const char* k, uint16_t d) { return get<uint16_t>(k, d); }
  void putUInt(const char* k, uint32_t v) { put(k, v); }
  uint32_t getUInt(const char* k, uint32_t d) { return get<uint32_t>(k, d); }
  void putBool(const char* k, bool v) { put(k, v); }
  bool getBool(const char* k, bool d) { return get<bool>(k, d); }
  void putString(const char* k, const String& v) { db()[ns + "/" + k] = v.s; }
  void putString(const char* k, const char* v) { db()[ns + "/" + k] = v; }
  String getString(const char* k, const String& d) { auto it = db().find(ns + "/" + k); return it == db().end() ? d : String(it->second); }
  size_t getString(const char* k, char* out, size_t n) { auto it = db().find(ns + "/" + k); std::string v = it == db().end() ? "" : it->second; strlcpy(out, v.c_str(), n); return v.size(); }
  void putBytes(const char* k, const void* b, size_t n) { db()[ns + "/" + k] = std::string((const char*)b, n); }
  size_t getBytesLength(const char* k) { auto it = db().find(ns + "/" + k); return it == db().end() ? 0 : it->second.size(); }
  void getBytes(const char* k, void* b, size_t n) { auto it = db().find(ns + "/" + k); if (it != db().end()) memcpy(b, it->second.data(), std::min(n, it->second.size())); }
};
