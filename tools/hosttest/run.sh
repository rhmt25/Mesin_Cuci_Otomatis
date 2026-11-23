#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Rahmat Hidayat
# Uji logika mesin di PC (butuh g++). Jalankan dari akar repo: sh tools/hosttest/run.sh [-v]
set -e
FW=firmware/mesin_cuci_otomatis
OUT=${TMPDIR:-/tmp}/mesin_cuci_hosttest
g++ -std=gnu++17 -O1 -Wall -Wno-unused-function -DHOST_TEST -DTIME_SCALE=60 \
  -Itools/hosttest/stubs -I.pio/libdeps/rymcu-esp32-devkitc/ArduinoJson/src -I$FW \
  tools/hosttest/test_machine.cpp $FW/machine.cpp $FW/tahap.cpp $FW/proses_store.cpp $FW/relay.cpp \
  $FW/water_sensor.cpp $FW/settings.cpp $FW/net_policy.cpp $FW/remote_policy.cpp $FW/backup.cpp -o "$OUT"
"$OUT" "$@"

# Uji katalog Tahap & penyimpanan Proses
OUT2=${TMPDIR:-/tmp}/mesin_cuci_hosttest_tahap
g++ -std=gnu++17 -O1 -Wall -Wno-unused-function -DHOST_TEST -DTIME_SCALE=60 \
  -Itools/hosttest/stubs -I.pio/libdeps/rymcu-esp32-devkitc/ArduinoJson/src -I$FW \
  tools/hosttest/test_tahap.cpp $FW/tahap.cpp $FW/proses_store.cpp $FW/settings.cpp -o "$OUT2"
"$OUT2"
