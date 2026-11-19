#!/bin/sh
# Uji logika mesin di PC (butuh g++). Jalankan dari akar repo: sh tools/hosttest/run.sh [-v]
set -e
FW=firmware/mesin_cuci_otomatis
OUT=${TMPDIR:-/tmp}/mesin_cuci_hosttest
g++ -std=gnu++17 -O1 -Wall -Wno-unused-function -DHOST_TEST -DTIME_SCALE=60 \
  -Itools/hosttest/stubs -I$FW \
  tools/hosttest/test_machine.cpp $FW/machine.cpp $FW/programs.cpp $FW/relay.cpp \
  $FW/water_sensor.cpp $FW/settings.cpp $FW/net_policy.cpp $FW/remote_policy.cpp -o "$OUT"
"$OUT" "$@"
