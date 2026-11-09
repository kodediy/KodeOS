#!/usr/bin/env python3
"""The larger line glyphs kgui draws, as A8 C images: the kit's own bodies
(sdk/components/kui/tools/glyphgen.py), drawn at their size so they are not a blurred
upscale. Run it as tools/glyphgen.py says (cairosvg needs an interpreter that keeps DYLD_*)."""
import importlib.util
import sys
from pathlib import Path

KGUI = Path(__file__).resolve().parent.parent
spec = importlib.util.spec_from_file_location("kitglyph", KGUI.parent.parent / "sdk/components/kui/tools/glyphgen.py")
kit = importlib.util.module_from_spec(spec)
sys.modules["kitglyph"] = kit
spec.loader.exec_module(kit)
WANT = [("power", 180), ("wifi", 46), ("bt", 46), ("more", 46), ("power", 46), ("share", 46)]
WANT += [(f"sun{i}", 52) for i in range(4)] + [(f"spk{i}", 52) for i in range(4)]
for name, size in WANT:
    alpha = kit.render(kit.G[name][1], size).getchannel("A")
    data = alpha.tobytes()
    sym = f"kgui_glyph_{name}_{size}"
    lines = ["    " + ",".join(f"0x{b:02x}" for b in data[i:i + 32]) + "," for i in range(0, len(data), 32)]
    (KGUI / "glyphs" / f"{sym}.c").write_text(f'''/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Written by tools/glyph_big.py: never by hand. */
#include "lvgl.h"

static const uint8_t {sym}_map[] = {{
''' + "\n".join(lines) + f'''
}};

const lv_image_dsc_t {sym} = {{
  .header = {{
    .magic = LV_IMAGE_HEADER_MAGIC,
    .cf = LV_COLOR_FORMAT_A8,
    .flags = 0,
    .w = {size},
    .h = {size},
    .stride = {size},
    .reserved_2 = 0,
  }},
  .data_size = sizeof({sym}_map),
  .data = {sym}_map,
  .reserved = NULL,
}};
''')
    print(sym, len(data))
