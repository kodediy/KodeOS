#!/usr/bin/env python3
"""kgui's own folder glyphs, the ones the kit has not got, as A8 C images.

    tools/glyphgen.py           write glyphs/assets/glyph_<name>_40.png, glyphs/kgui_glyph_<name>_40.c
                                and glyphs/kgui_glyphs.h
    tools/glyphgen.py --check   exit 1 when a PNG or the header on disk is not what this script draws

The drawing is the kit's own (sdk/components/kui/tools/glyphgen.py, render()):
an SVG body on a 24 x 24 grid, 2.2 px round strokes, white on transparent, its
alpha the glyph. The C images are written by LVGL's LVGLImage.py as A8, the
format of the kit's line glyphs, so a screen recolours them as it draws them
(lv_obj_set_style_image_recolor() at LV_OPA_COVER). They live in kgui and not
in the kit because the SDK is not kgui's to change; the names are kgui's
(kgui_glyph_<name>_40) so a later kit glyph of the same name does not clash.

The twelve bodies are the launcher design's of 5-nov-2026 (kode-dot-os,
informes/2026-11-05-lanzador/DISENO.md, render/glifos.py); `clock` is the
kit's, which the kit draws at 30 only.

Needs cairosvg, Pillow, pypng and lz4. cairosvg loads libcairo by name: on
macOS with Homebrew's cairo, run it with an interpreter that keeps DYLD_*
(not /usr/bin/python3, whose SIP strips it), for instance

    DYLD_FALLBACK_LIBRARY_PATH=/opt/homebrew/lib \\
        /Library/Developer/CommandLineTools/usr/bin/python3 tools/glyphgen.py

The PNGs, the C files and the header are checked in, so building never needs
any of this.
"""
import argparse
import io
import subprocess
import sys
from pathlib import Path

import cairosvg
from PIL import Image

KGUI = Path(__file__).resolve().parent.parent
OUT = KGUI / "glyphs"
ASSETS = OUT / "assets"
HEADER = OUT / "kgui_glyphs.h"
SPDX = "/* SPDX-License-Identifier: GPL-3.0-or-later */"
LVGL_IMAGE = KGUI.parent.parent / "managed_components" / "lvgl__lvgl" / "scripts" / "LVGLImage.py"
SIZE = 40

# name -> (what it stands for, SVG body)
G = {
    "gauge": ("A dial: the Sensors folder", '<path d="M4.6 18A9.2 9.2 0 1 1 19.4 18"/><path d="M12 12.6l4-4.2"/><circle cx="12" cy="12.6" r="1.8" fill="currentColor" stroke="none"/>'),
    "antenna": ("An antenna: the Radio folder", '<circle cx="12" cy="9" r="1.7" fill="currentColor" stroke="none"/><path d="M12 11.5v9"/><path d="M8.8 5.8a4.5 4.5 0 0 0 0 6.4M15.2 5.8a4.5 4.5 0 0 1 0 6.4"/><path d="M5.9 2.9a8.6 8.6 0 0 0 0 12.2M18.1 2.9a8.6 8.6 0 0 1 0 12.2"/>'),
    "shield": ("A shield with a keyhole: the Security folder", '<path d="M12 2.8l7.8 3v5.6c0 4.8-3.3 8.3-7.8 9.8-4.5-1.5-7.8-5-7.8-9.8V5.8z"/><circle cx="12" cy="10.4" r="1.9" fill="currentColor" stroke="none"/><path d="M12 11.5v3.6"/>'),
    "note": ("Two notes: the Music folder", '<path d="M9 17.5V6.5l11-2.5v11.5"/><circle cx="6.5" cy="17.5" r="2.5"/><circle cx="17.5" cy="15.5" r="2.5"/>'),
    "camera": ("A camera: the Camera folder", '<path d="M4.5 7.5h2.8l1.7-2.5h6l1.7 2.5h2.8a1.5 1.5 0 0 1 1.5 1.5v9a1.5 1.5 0 0 1-1.5 1.5h-15A1.5 1.5 0 0 1 3 18V9a1.5 1.5 0 0 1 1.5-1.5z"/><circle cx="12" cy="13.2" r="3.6"/>'),
    "robot": ("A robot's head: the Robotics folder", '<rect x="5" y="8" width="14" height="11.5" rx="3"/><path d="M12 8V5.6"/><circle cx="12" cy="4" r="1.5" fill="currentColor" stroke="none"/><circle cx="9.4" cy="13.4" r="1.5" fill="currentColor" stroke="none"/><circle cx="14.6" cy="13.4" r="1.5" fill="currentColor" stroke="none"/><path d="M2.6 12.2v3.2M21.4 12.2v3.2"/>'),
    "home": ("A house: the Home folder", '<path d="M3.8 10.6L12 3.8l8.2 6.8v8.4a1.5 1.5 0 0 1-1.5 1.5H5.3a1.5 1.5 0 0 1-1.5-1.5z"/><path d="M9.6 20.5v-5.6h4.8v5.6"/>'),
    "heart": ("A heart with its pulse: the Health folder", '<path d="M12 20.2C7 17 3.5 13.6 3.5 9.4A4.6 4.6 0 0 1 12 6.8a4.6 4.6 0 0 1 8.5 2.6c0 4.2-3.5 7.6-8.5 10.8z"/><path d="M6.5 12.2h3l1.5-2.4 2 4.4 1.5-2h3"/>'),
    "pin": ("A map pin: the Navigation folder", '<path d="M12 21c-3.6-3.6-6.6-7.2-6.6-11a6.6 6.6 0 0 1 13.2 0c0 3.8-3 7.4-6.6 11z"/><circle cx="12" cy="10" r="2.4"/>'),
    "clock": ("A clock: the Time folder (the kit's body, at 40)", '<circle cx="12" cy="12" r="9"/><path d="M12 7v5l3 2"/>'),
    "list": ("A checklist: the Productivity folder", '<path d="M3.8 7.2l1.9 1.9 3.3-3.4"/><path d="M3.8 15.2l1.9 1.9 3.3-3.4"/><path d="M12.5 7.6h7.7M12.5 15.6h7.7"/>'),
    "book": ("An open book: the Education folder", '<path d="M12 6.8C9.8 5.2 6.8 4.7 3.5 5.1v13.2c3.3-.4 6.3.1 8.5 1.7 2.2-1.6 5.2-2.1 8.5-1.7V5.1c-3.3-.4-6.3.1-8.5 1.7z"/><path d="M12 6.8V20"/>'),
    "code": ("Code brackets: the Developer folder", '<path d="M7.5 7L2.8 12l4.7 5M16.5 7l4.7 5-4.7 5"/><path d="M13.6 4.6l-3.2 14.8"/>'),
}


def render(body, size):
    """The glyph as a white RGBA picture whose alpha is the drawing (the kit's render())."""
    svg = (f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" width="{size}" height="{size}" '
           f'color="#fff"><g fill="none" stroke="#fff" stroke-width="2.2" stroke-linecap="round" '
           f'stroke-linejoin="round">{body.replace("currentColor", "#fff")}</g></svg>')
    png = cairosvg.svg2png(bytestring=svg.encode(), output_width=size, output_height=size)
    alpha = Image.open(io.BytesIO(png)).getchannel("A")
    out = Image.new("RGBA", alpha.size, (255, 255, 255, 0))
    out.putalpha(alpha)
    return out


def png_bytes(image):
    buf = io.BytesIO()
    image.save(buf, format="PNG", optimize=False)
    return buf.getvalue()


def header_text():
    lines = [
        "/* SPDX-License-Identifier: GPL-3.0-or-later */",
        "/**",
        " * @file kgui_glyphs.h",
        " * @brief kgui's own folder glyphs: A8 images of one stroke, recoloured as they are drawn.",
        " *",
        " * Generated by tools/glyphgen.py. Do not edit.",
        " */",
        "#ifndef KGUI_GLYPHS_H",
        "#define KGUI_GLYPHS_H",
        "",
        '#include "lvgl.h"',
        "",
        "#ifdef __cplusplus",
        'extern "C" {',
        "#endif",
        "",
    ]
    for n, (what, _) in G.items():
        lines.append(f"extern const lv_image_dsc_t kgui_glyph_{n}_{SIZE}; /**< {what}. */")
    lines += ["", "#ifdef __cplusplus", "}", "#endif", "", "#endif /* KGUI_GLYPHS_H */", ""]
    return "\n".join(lines)


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--check", action="store_true")
    check = ap.parse_args().check
    jobs = [(ASSETS / f"glyph_{n}_{SIZE}.png", png_bytes(render(b, SIZE))) for n, (_, b) in G.items()]
    jobs.append((HEADER, header_text().encode("utf-8")))
    if check:
        stale = [p for p, d in jobs if not p.exists() or p.read_bytes() != d]
        for p in stale:
            print(f"{p.relative_to(KGUI)} is stale: run tools/glyphgen.py")
        return 1 if stale else 0
    ASSETS.mkdir(parents=True, exist_ok=True)
    for path, data in jobs:
        path.write_bytes(data)
    for n in G:
        subprocess.run([sys.executable, str(LVGL_IMAGE), "--ofmt", "C", "--cf", "A8", "-o", str(OUT),
                        "--name", f"kgui_glyph_{n}_{SIZE}", str(ASSETS / f"glyph_{n}_{SIZE}.png")],
                       check=True)
        c = OUT / f"kgui_glyph_{n}_{SIZE}.c"
        c.write_text(SPDX + "\n" + c.read_text())
    print(f"wrote {len(G)} glyphs and {HEADER.relative_to(KGUI)}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
