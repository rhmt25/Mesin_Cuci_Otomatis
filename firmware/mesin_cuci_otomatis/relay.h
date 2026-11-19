#pragma once
// Driver relay (trigger LOW, open-drain) + interlock keselamatan.
#include <Arduino.h>

enum RelayId : uint8_t { R_LEFT, R_RIGHT, R_FILL, R_DRAIN, R_SPIN, R_PERFUME, R_COUNT };

void relayInit();                       // panggil paling awal di setup()
bool relaySet(RelayId id, bool on);     // false bila ditolak interlock
void relayAllOff();
bool relayIsOn(RelayId id);
const char* relayName(RelayId id);
int relayFromName(const String& name);  // -1 bila tidak dikenal
uint32_t relayMotorSeconds();           // akumulasi motor cuci menyala (detik, nyata)
