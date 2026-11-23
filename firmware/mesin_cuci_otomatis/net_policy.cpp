// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rahmat Hidayat
#include "net_policy.h"

void connWdInit(ConnWatchdog& w) {
  w.armed = false;
  w.failStreak = 0;
  w.reconnectsNoSuccess = 0;
}

ConnAction connWdReport(ConnWatchdog& w, bool pingOk) {
  if (pingOk) {
    w.armed = true;
    w.failStreak = 0;
    w.reconnectsNoSuccess = 0;
    return CONN_NONE;
  }
  if (!w.armed) return CONN_NONE;   // gateway belum pernah menjawab ping: jangan menyalahkan jaringan
  if (++w.failStreak < CONN_FAILS_BEFORE_RECONNECT) return CONN_NONE;
  w.failStreak = 0;
  if (++w.reconnectsNoSuccess >= CONN_RECONNECTS_BEFORE_RESTART) {
    w.reconnectsNoSuccess = 0;
    return CONN_RESTART_WIFI;
  }
  return CONN_RECONNECT;
}

void apPolicyInit(ApPolicy& p) {
  p.apOn = false;
  p.staUp = false;
  p.staUpSince = 0;
  p.lastClientMs = 0;
}

bool apPolicyStep(ApPolicy& p, uint32_t nowMs, bool staConnected, bool allSsidFailed, int apClients) {
  if (!staConnected) {
    p.staUp = false;
    if (allSsidFailed) p.apOn = true;   // semua SSID gagal: buka jalur akses darurat
    return p.apOn;
  }
  if (!p.staUp) {
    p.staUp = true;
    p.staUpSince = nowMs;
  }
  if (apClients > 0) p.lastClientMs = nowMs;
  if (p.apOn) {
    uint32_t idleFrom = p.lastClientMs > p.staUpSince ? p.lastClientMs : p.staUpSince;
    if (apClients == 0 && nowMs - idleFrom >= AP_IDLE_OFF_MS) p.apOn = false;
  }
  return p.apOn;
}
