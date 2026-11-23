#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Rahmat Hidayat
"""Gabungkan web/index.html + style.css + app.js menjadi satu halaman, gzip, lalu
tulis firmware/mesin_cuci_otomatis/web_ui.h. Jalankan setiap selesai mengubah web/.

    python tools/build_web.py            # web_ui.h untuk ESP32
    python tools/build_web.py --remote   # + cloud/public/index.html untuk Worker (UI jarak jauh, kode sama)
"""
import gzip
import re
import pathlib
import sys

root = pathlib.Path(__file__).resolve().parent.parent
web = root / "web"
out = root / "firmware" / "mesin_cuci_otomatis" / "web_ui.h"

html = (web / "index.html").read_text(encoding="utf-8")
css = (web / "style.css").read_text(encoding="utf-8")
js = (web / "app.js").read_text(encoding="utf-8")


def slim(text, line_comments):
    """Buang komentar satu baris penuh dan indentasi awal (aman: tanpa mengubah string/regex)."""
    out = []
    for ln in text.split("\n"):
        t = ln.strip()
        if not t or (line_comments and t.startswith("//")):
            continue
        out.append(t)
    return "\n".join(out)


def slim_css(text):
    text = re.sub(r"/\*.*?\*/", "", text, flags=re.S)
    return slim(text, False)


html = html.replace("/*CSS*/", slim_css(css)).replace("/*JS*/", slim(js, True))

data = gzip.compress(html.encode("utf-8"), compresslevel=9, mtime=0)
lines = []
for i in range(0, len(data), 16):
    lines.append("  " + ",".join("0x%02x" % b for b in data[i:i + 16]))

out.write_text(
    "// SPDX-License-Identifier: GPL-3.0-or-later\n"
    "// Copyright (C) 2026 Rahmat Hidayat\n"
    "#pragma once\n"
    "// FILE HASIL GENERATE oleh tools/build_web.py dari folder web/. Jangan diedit manual.\n"
    "#include <Arduino.h>\n"
    "static const size_t WEB_UI_GZ_LEN = %d;\n"
    "static const uint8_t WEB_UI_GZ[] PROGMEM = {\n%s\n};\n" % (len(data), ",\n".join(lines)),
    encoding="utf-8",
)
print("web_ui.h: %d byte gzip (%d byte asli)" % (len(data), len(html.encode("utf-8"))))

if "--remote" in sys.argv:
    pub = root / "cloud" / "public"
    pub.mkdir(parents=True, exist_ok=True)
    (pub / "index.html").write_text(html, encoding="utf-8")
    print("cloud/public/index.html: %d byte" % len(html.encode("utf-8")))
