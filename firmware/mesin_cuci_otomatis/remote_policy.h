// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rahmat Hidayat
#pragma once
// Allowlist untuk permintaan dari jarak jauh (lewat Worker). Murni (tanpa Arduino) agar bisa diuji di PC,
// dan HARUS identik dengan cloud/src/allow.js (diuji bersama dengan cloud/test/allow-vectors.txt).
#include <stddef.h>

bool remoteCmdAllowed(const char* rawCmd);
// method: "GET"/"POST"/"DELETE"; cmd hanya dipakai untuk POST /api/cmd (boleh nullptr).
bool remoteApiAllowed(const char* method, const char* path, const char* cmd);
