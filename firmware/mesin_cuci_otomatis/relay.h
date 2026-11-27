// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rahmat Hidayat
#pragma once
// Driver relay (trigger LOW, open-drain) + interlock keselamatan.
#include <Arduino.h>

enum RelayId : uint8_t { R_LEFT, R_RIGHT, R_FILL, R_DRAIN, R_SPIN, R_PERFUME, R_COUNT };

void relayInit();                       // panggil paling awal di setup()
bool relaySet(RelayId id, bool on);     // false bila ditolak interlock
const char* relayCheck(RelayId id, bool on);  // alasan penolakan bila dicoba sekarang (nullptr = boleh), tanpa mengubah apa pun
void relayAllOff();
void relayOffMask(uint8_t mask);        // matikan sebagian (bit = RelayId), dipakai per workspace
bool relayIsOn(RelayId id);
const char* relayName(RelayId id);
int relayFromName(const String& name);  // -1 bila tidak dikenal
uint32_t relayMotorSeconds();           // akumulasi motor cuci menyala (detik, nyata)

// Failsafe berlapis. Panggil tiap tick mesin.
//   0 = aman
//   1 = motor cuci menyala terlalu lama -> semua relay dimatikan (tidak terkunci)
//   3 = pompa pewangi menyala terlalu lama -> hanya pompa dimatikan (tidak terkunci)
//   4 = motor spin menyala terlalu lama -> hanya motor spin dimatikan (tidak terkunci)
//   2 = pelanggaran interlock (mis. motor kiri & kanan nyala bersamaan) -> semua relay
//       dimatikan dan TERKUNCI sampai mesin di-restart
uint8_t relayGuard();
bool relayFaulted();
