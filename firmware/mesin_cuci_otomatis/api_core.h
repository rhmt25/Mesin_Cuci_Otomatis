#pragma once
// Dispatcher API tunggal: dipakai oleh HTTP lokal (web_server.cpp) dan jarak jauh (remote.cpp, lewat Worker).
// Autentikasi dilakukan oleh pemanggil (cookie lokal / Worker). Permintaan REMOTE dicek allowlist di sini.
#include <Arduino.h>
#include <functional>

enum ApiSource : uint8_t { API_LOCAL, API_REMOTE };

struct ApiResult {
  int code;
  String body;   // selalu JSON
};

typedef std::function<String(const char*)> QueryFn;   // nilai parameter query, "" bila tidak ada

ApiResult apiHandle(ApiSource src, const char* method, const String& path, const QueryFn& query, const String& body);

String apiStatusJson();                                                 // status mesin (dipakai protokol remote)
String apiLogsJson(uint32_t since, size_t maxCount, uint32_t* lastOut = nullptr);
bool apiLowMemory(size_t needBytes);
