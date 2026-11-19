#!/usr/bin/env python3
"""Gabungkan web/index.html + style.css + app.js menjadi satu halaman, gzip, lalu
tulis firmware/mesin_cuci_otomatis/web_ui.h. Jalankan setiap selesai mengubah web/.

    python tools/build_web.py
"""
import gzip
import pathlib

root = pathlib.Path(__file__).resolve().parent.parent
web = root / "web"
out = root / "firmware" / "mesin_cuci_otomatis" / "web_ui.h"

html = (web / "index.html").read_text(encoding="utf-8")
css = (web / "style.css").read_text(encoding="utf-8")
js = (web / "app.js").read_text(encoding="utf-8")
html = html.replace("/*CSS*/", css).replace("/*JS*/", js)

data = gzip.compress(html.encode("utf-8"), compresslevel=9, mtime=0)
lines = []
for i in range(0, len(data), 16):
    lines.append("  " + ",".join("0x%02x" % b for b in data[i:i + 16]))

out.write_text(
    "#pragma once\n"
    "// FILE HASIL GENERATE oleh tools/build_web.py dari folder web/. Jangan diedit manual.\n"
    "#include <Arduino.h>\n"
    "static const size_t WEB_UI_GZ_LEN = %d;\n"
    "static const uint8_t WEB_UI_GZ[] PROGMEM = {\n%s\n};\n" % (len(data), ",\n".join(lines)),
    encoding="utf-8",
)
print("web_ui.h: %d byte gzip (%d byte asli)" % (len(data), len(html.encode("utf-8"))))
