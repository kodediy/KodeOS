#!/usr/bin/env python3
"""The launcher's folder art: two inks on a light disc, as ARGB8888 C images.

    tools/folder_art.py                  write glyphs/kgui_folder_<key>.c and kgui_folder_art.{h,c}
    tools/folder_art.py --check          exit 1 when a file on disk is not what this script draws
    tools/folder_art.py --sheet OUT.png  also write the review sheet
    tools/folder_art.py --png DIR        also write each image as DIR/<key>.png

Each folder (the keys of s_art, kgui_group.c) is a Soft Black drawing with one
touch of Bold Yellow that says something: the needle that moves, the window
with the light on, the eye that winks. The image is the ink alone, on
transparent; the screen draws the Medium Grey disc under it.

Yellow and the disc have almost the same luminance (0.51 and 0.50), so yellow
never sits bare on the disc at this size: it lies on black or inside a black
line, or it is not seen. That is why these are heavier than the one-ink line
glyphs of tools/glyphgen.py, though they come from the same family: a 24 x 24
grid, round caps and joins, 2.2 px lines where a line is drawn.

A drawing is a list of layers, painted in order:

    K  Soft Black
    Y  Bold Yellow
    C  cut: takes out what is under it, so the disc shows through

Each is drawn at 8x and the whole brought down in premultiplied alpha, so an
edge between the two inks stays clean and the alpha stays straight. Fully
transparent pixels are written as 0,0,0,0.

Needs cairosvg and Pillow, and an interpreter that keeps DYLD_* for cairosvg,
as tools/glyphgen.py explains:

    DYLD_FALLBACK_LIBRARY_PATH=/opt/homebrew/lib \\
        /Library/Developer/CommandLineTools/usr/bin/python3 -B tools/folder_art.py

The C files are checked in, so building never needs any of this.
"""
import argparse
import io
import sys
from pathlib import Path

import cairosvg
from PIL import Image, ImageChops, ImageDraw, ImageFont

KGUI = Path(__file__).resolve().parent.parent
OUT = KGUI / "glyphs"
SIZE = 40
SS = 8
INK = (0x19, 0x19, 0x19)      # Soft Black
GOLD = (0xFF, 0xAC, 0x00)     # Bold Yellow
DISC = (0xC2, 0xBA, 0xB3)     # Medium Grey, the disc's colour: the sheet only
BRAND_FONT = Path.home() / "Documents/Kode/5-Marca/kode-brand/fonts/outfit/Outfit[wght].ttf"


def fill(body, edge=0.0):
    """Shapes filled solid, with a line around them to round their corners."""
    return f'<g fill="#fff" stroke-width="{edge}">{body}</g>'


def line(body, width=2.2):
    """Shapes drawn as a line."""
    return f'<g stroke-width="{width}">{body}</g>'


def turn(body, deg):
    """A shape drawn upright and then leaned over, about the grid's centre."""
    return f'<g transform="rotate({deg} 12 12)">{body}</g>'


def at(body, dx, dy, scale=1.0):
    """A shape moved on the grid and scaled about its centre, to sit it in its optical place."""
    return f'<g transform="translate({12 + dx} {12 + dy}) scale({scale}) translate(-12 -12)">{body}</g>'


# key -> (the title, the idea in a line, the layers)
ART = {
    "games": ("Games", "The pad with the one button you are about to press lit", [
        ("K", fill('<path d="M7 6.5h10a4.5 4.5 0 0 1 4.4 3.6l1 5.2a2.6 2.6 0 0 1-4.5 2.2L16 15.5H8l-1.9 2a2.6 2.6 0 0 1-4.5-2.2l1-5.2A4.5 4.5 0 0 1 7 6.5z"/>', 1.2)),
        ("C", line('<path d="M7.6 9.3v4.2M5.5 11.4h4.2"/>', 1.8)),
        ("Y", fill('<circle cx="16.4" cy="11.2" r="2.4"/>')),
    ]),
    "tools": ("Tools", "The wrench tightening a yellow nut", [
        ("K", at(turn(line('<path d="M12 22.2V11"/>', 4.0) + fill('<circle cx="12" cy="6.4" r="7.3"/>'), 45), -0.5, 0.5)),
        ("C", at(turn(fill('<path d="M8 -3h8v10L12 10 8 7z"/>'), 45), -0.5, 0.5)),
        ("K", at(turn(fill('<path d="M12 1.2l3.46 2v4l-3.46 2-3.46-2v-4z"/>', 1.0), 45), -0.5, 0.5)),
        ("Y", at(turn(fill('<path d="M12 1.2l3.46 2v4l-3.46 2-3.46-2v-4z"/>'), 45), -0.5, 0.5)),
        ("K", at(turn(fill('<circle cx="12" cy="5.2" r="1.4"/>'), 45), -0.5, 0.5)),
    ]),
    "communication": ("Communication", "A big bubble, and the small one that answers it", [
        ("K", fill('<path d="M6 3h9.2a3.6 3.6 0 0 1 3.6 3.6v4.6a3.6 3.6 0 0 1-3.6 3.6H8.8l-4.2 3.4v-3.6A3.6 3.6 0 0 1 2.4 11.2V6.6A3.6 3.6 0 0 1 6 3z"/>', 0.8)),
        ("K", fill('<path d="M14 11.6h5a2.4 2.4 0 0 1 2.4 2.4v2.4a2.4 2.4 0 0 1-1.9 2.35V21.4l-2.9-2.6H14a2.4 2.4 0 0 1-2.4-2.4V14a2.4 2.4 0 0 1 2.4-2.4z"/>', 2.2)),
        ("Y", fill('<path d="M14 11.6h5a2.4 2.4 0 0 1 2.4 2.4v2.4a2.4 2.4 0 0 1-1.9 2.35V21.4l-2.9-2.6H14a2.4 2.4 0 0 1-2.4-2.4V14a2.4 2.4 0 0 1 2.4-2.4z"/>')),
    ]),
    "wireless": ("Wireless", "The signal fan, its nearest wave a warm wedge", [
        ("K", at(line('<path d="M2.2 8.8a16 16 0 0 1 19.6 0"/><path d="M5.7 12.7a10.6 10.6 0 0 1 12.6 0"/>'), 0, -1)),
        ("K", at(fill('<path d="M12 21L8 16.6a5.9 5.9 0 0 1 8 0z"/>', 1.8), 0, -1)),
        ("Y", at(fill('<path d="M12 21L8 16.6a5.9 5.9 0 0 1 8 0z"/>'), 0, -1)),
    ]),
    "creative": ("Creative", "A brush just dipped in the yellow", [
        ("K", turn(fill('<path d="M12 -1.5c1.4 0 2.2 7 2.2 12.5h-4.4c0-5.5.8-12.5 2.2-12.5z"/>', 1.0)
                   + fill('<rect x="9.3" y="11" width="5.4" height="3.4" rx=".5"/>', 0.8), 45)),
        ("K", turn(fill('<path d="M9 15.4h6c.6 3-.8 6.5-3 9.2-2.2-2.7-3.6-6.2-3-9.2z"/>', 1.5), 45)),
        ("Y", turn(fill('<path d="M9 15.4h6c.6 3-.8 6.5-3 9.2-2.2-2.7-3.6-6.2-3-9.2z"/>'), 45)),
    ]),
    "maker": ("Maker", "A chip the moment you give it power", [
        ("K", line('<path d="M9.5 2.8v3M14.5 2.8v3M9.5 18.2v3M14.5 18.2v3M2.8 9.5h3M2.8 14.5h3M18.2 9.5h3M18.2 14.5h3"/>', 2.0)),
        ("K", fill('<rect x="5.4" y="5.4" width="13.2" height="13.2" rx="2"/>', 1.0)),
        ("Y", fill('<path d="M13.2 6.8L8.2 12.9h3l-.9 4.3 5.5-6.3h-3.1z"/>', 0.3)),
    ]),
    "ai": ("AI", "The small spark jumping off the big one", [
        ("K", at(fill('<path d="M9.6 5.2Q10.6 12.3 18 13.8Q10.6 15.3 9.6 22.4Q8.6 15.3 1.2 13.8Q8.6 12.3 9.6 5.2z"/>', 1.8), 0, 0, 0.9)),
        ("K", at(fill('<path d="M17.8 .9Q18.6 5.1 22.8 6Q18.6 6.9 17.8 11.1Q17 6.9 12.8 6Q17 5.1 17.8 .9z"/>', 2.0), 0, 0, 0.9)),
        ("Y", at(fill('<path d="M17.8 .9Q18.6 5.1 22.8 6Q18.6 6.9 17.8 11.1Q17 6.9 12.8 6Q17 5.1 17.8 .9z"/>', 0.3), 0, 0, 0.9)),
    ]),
    "sensors": ("Sensors", "The needle: the only thing on the dial that moves", [
        ("K", at(fill('<path d="M4.3 18.4A9.4 9.4 0 1 1 19.7 18.4z"/>', 1.6), 0, 1)),
        ("C", at(line('<path d="M3.5 12.9h2.4M12 4.3v2.4M20.5 12.9h-2.4"/>', 1.3), 0, 1)),
        ("Y", at(line('<path d="M12 13.4l4-4.8"/>', 2.2) + fill('<circle cx="12" cy="13.4" r="2.2"/>'), 0, 1)),
    ]),
    "radio": ("Radio", "A mast and the wave that has just left it", [
        ("K", line('<path d="M7 21.2L11.2 10.2M17 21.2L12.8 10.2M8.8 17h6.4"/>')),
        ("K", line('<path d="M3.9 2.9a9.6 9.6 0 0 0 0 10.2M20.1 2.9a9.6 9.6 0 0 1 0 10.2"/>')),
        ("K", line('<path d="M7.7 4.4a5.6 5.6 0 0 0 0 7.2M16.3 4.4a5.6 5.6 0 0 1 0 7.2"/>', 3.4)),
        ("Y", line('<path d="M7.7 4.4a5.6 5.6 0 0 0 0 7.2M16.3 4.4a5.6 5.6 0 0 1 0 7.2"/>', 1.5)),
        ("K", fill('<circle cx="12" cy="8" r="2.3"/>')),
    ]),
    "security": ("Security", "A shield whose keyhole has the light behind it", [
        ("K", fill('<path d="M12 2.8l7.8 3v5.6c0 4.8-3.3 8.3-7.8 9.8-4.5-1.5-7.8-5-7.8-9.8V5.8z"/>', 1.4)),
        ("Y", fill('<circle cx="12" cy="9.8" r="2.7"/><path d="M10.8 10.8L9.8 16.4h4.4L13.2 10.8z"/>', 0.3)),
    ]),
    "music": ("Music", "A note whose flag is the part that dances", [
        ("K", line('<path d="M11.4 18.2V3.4"/>')),
        ("K", fill('<ellipse cx="7.8" cy="18.4" rx="4" ry="3.2" transform="rotate(-18 7.8 18.4)"/>', 0.8)),
        ("K", fill('<path d="M12 3c.5 3.2 2.6 4.5 4.3 6 1.9 1.7 2.9 3.8 1.7 7.4 0-3-1.7-4.6-3.3-5.3-1-.4-1.9-.7-2.7-.8z"/>', 2.2)),
        ("Y", fill('<path d="M12 3c.5 3.2 2.6 4.5 4.3 6 1.9 1.7 2.9 3.8 1.7 7.4 0-3-1.7-4.6-3.3-5.3-1-.4-1.9-.7-2.7-.8z"/>')),
    ]),
    "camera": ("Camera", "A lens looking back at you", [
        ("K", fill('<path d="M4.5 7.5h2.8l1.7-2.5h6l1.7 2.5h2.8a1.5 1.5 0 0 1 1.5 1.5v9a1.5 1.5 0 0 1-1.5 1.5h-15A1.5 1.5 0 0 1 3 18V9a1.5 1.5 0 0 1 1.5-1.5z"/>', 1.4)),
        ("C", fill('<circle cx="18.6" cy="10.3" r="1"/>')),
        ("Y", line('<circle cx="11.4" cy="13.2" r="3.6"/>', 2.2)),
    ]),
    "robotics": ("Robotics", "A robot that winks at you", [
        ("K", line('<path d="M12 7v-3"/>') + fill('<circle cx="12" cy="2.9" r="1.7"/>')),
        ("K", line('<path d="M1.8 12.4v4.2M22.2 12.4v4.2"/>')),
        ("K", fill('<rect x="4.4" y="6.8" width="15.2" height="14.4" rx="3.4"/>', 1.0)),
        ("Y", fill('<circle cx="8.7" cy="12.8" r="2.5"/>')),
        ("Y", line('<path d="M13 13.8q2.3-3.2 4.6 0"/>', 2.2)),
        ("C", line('<path d="M9.6 17.8h4.8"/>', 1.5)),
    ]),
    "home": ("Home", "The window with the light on: somebody is home", [
        ("K", fill('<path d="M16 4.2h2.8v5h-2.8z"/>', 0.6)),
        ("K", fill('<path d="M3.8 10.6L12 3.8l8.2 6.8v8.4a1.5 1.5 0 0 1-1.5 1.5H5.3a1.5 1.5 0 0 1-1.5-1.5z"/>', 1.2)),
        ("Y", fill('<rect x="8.7" y="11" width="6.6" height="6.2" rx=".5"/>')),
        ("K", line('<path d="M12 11v6.2M8.7 14.1h6.6"/>', 1.0)),
    ]),
    "health": ("Health", "A heart with its pulse going through it", [
        ("K", fill('<path d="M12 20.2C7 17 3.5 13.6 3.5 9.4A4.6 4.6 0 0 1 12 6.8a4.6 4.6 0 0 1 8.5 2.6c0 4.2-3.5 7.6-8.5 10.8z"/>', 1.6)),
        ("Y", line('<path d="M5 12.2h3.2l1.6-3 2.6 6 1.8-3h4.8"/>', 1.8)),
    ]),
    "navigation": ("Navigation", "A pin landing on the yellow spot where you are going", [
        ("K", at(fill('<ellipse cx="12" cy="19.6" rx="7" ry="2.7"/>', 1.8), 0, -0.5)),
        ("Y", at(fill('<ellipse cx="12" cy="19.6" rx="7" ry="2.7"/>'), 0, -0.5)),
        ("K", at(fill('<path d="M12 19.4C8.4 15.6 5.6 12.4 5.6 8.6a6.4 6.4 0 0 1 12.8 0c0 3.8-2.8 7-6.4 10.8z"/>', 0.8), 0, -0.5)),
        ("C", at(fill('<circle cx="12" cy="8.6" r="2.5"/>'), 0, -0.5)),
    ]),
    "time": ("Time", "The sand still falling", [
        ("Y", fill('<path d="M12 15.2c1.8 1.2 3.8 2.9 4.2 5.3H7.8c.4-2.4 2.4-4.1 4.2-5.3z"/><path d="M8.8 6.8h6.4c-.6 1.7-1.8 2.9-3.2 4.1-1.4-1.2-2.6-2.4-3.2-4.1z"/>')),
        ("Y", line('<path d="M12 11v5"/>', 1.2)),
        ("K", line('<path d="M6.9 3.2c0 4.2 2.1 6.5 5.1 8.8 3-2.3 5.1-4.6 5.1-8.8M6.9 20.8c0-4.2 2.1-6.5 5.1-8.8 3 2.3 5.1 4.6 5.1 8.8"/>', 1.8)),
        ("K", line('<path d="M5 3h14M5 21h14"/>')),
    ]),
    "productivity": ("Productivity", "The task, done", [
        ("K", fill('<rect x="4.8" y="4.6" width="14.4" height="17" rx="2.6"/>', 0.8)),
        ("C", fill('<rect x="7.8" y="1" width="8.4" height="6.4" rx="1.6"/>')),
        ("K", fill('<rect x="9.2" y="2.2" width="5.6" height="3.8" rx="1"/>', 0.6)),
        ("Y", line('<path d="M8 14.2l2.9 3 5.1-6"/>', 2.6)),
    ]),
    "education": ("Education", "The ribbon that keeps your page", [
        ("K", at(fill('<path d="M12 6.8C9.8 5.2 6.8 4.7 2.9 5.1v12.8c3.9-.4 6.9.1 9.1 1.7 2.2-1.6 5.2-2.1 9.1-1.7V5.1c-3.9-.4-6.9.1-9.1 1.7z"/>', 1.2), 0, -1.4)),
        ("K", at(fill('<path d="M10.3 8h3.4v15.2L12 21.3l-1.7 1.9z"/>', 1.2), 0, -1.4)),
        ("Y", at(fill('<path d="M10.3 8h3.4v15.2L12 21.3l-1.7 1.9z"/>'), 0, -1.4)),
    ]),
    "developer": ("Developer", "A prompt, and the cursor blinking on it", [
        ("K", fill('<rect x="2.6" y="4.4" width="18.8" height="15.2" rx="3"/>', 1.0)),
        ("C", line('<path d="M6.6 8.8l3.4 3.2-3.4 3.2"/>')),
        ("Y", fill('<rect x="12" y="13.4" width="6" height="2.6" rx=".4"/>')),
    ]),
    "general": ("General", "Three in a row and the one that does not fit", [
        ("K", fill('<rect x="3.2" y="3.2" width="6.8" height="6.8" rx="1.3"/>', 1.0)),
        ("K", fill('<rect x="13.2" y="3.2" width="6.8" height="6.8" rx="1.3"/>', 1.0)),
        ("K", fill('<rect x="3.2" y="13.2" width="6.8" height="6.8" rx="1.3"/>', 1.0)),
        ("K", fill('<rect x="14" y="14" width="6.8" height="6.8" rx="1.3" transform="rotate(16 17.4 17.4)"/>', 2.0)),
        ("Y", fill('<rect x="14" y="14" width="6.8" height="6.8" rx="1.3" transform="rotate(16 17.4 17.4)"/>')),
    ]),
}


def mask(layer):
    """A layer's coverage at 8x: its alpha, the drawing."""
    px = SIZE * SS
    svg = (f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" width="{px}" height="{px}">'
           f'<g fill="none" stroke="#fff" stroke-linecap="round" stroke-linejoin="round">{layer}</g></svg>')
    png = cairosvg.svg2png(bytestring=svg.encode(), output_width=px, output_height=px)
    return Image.open(io.BytesIO(png)).getchannel("A")


def render(layers):
    """A drawing as a 40 x 40 RGBA picture in straight alpha."""
    px = SIZE * SS
    out = Image.new("RGBA", (px, px), (0, 0, 0, 0))
    for op, layer in layers:
        m = mask(layer)
        if op == "C":
            out.putalpha(ImageChops.multiply(out.getchannel("A"), ImageChops.invert(m)))
        else:
            ink = Image.new("RGBA", (px, px), (INK if op == "K" else GOLD) + (255,))
            ink.putalpha(m)
            out = Image.alpha_composite(out, ink)
    small = out.convert("RGBa").resize((SIZE, SIZE), Image.BOX).convert("RGBA")
    data = bytearray(small.tobytes())
    for i in range(0, len(data), 4):
        if data[i + 3] == 0:
            data[i:i + 3] = b"\0\0\0"
    return Image.frombytes("RGBA", small.size, bytes(data))


def c_image(key, image):
    """The picture as a C image: B, G, R, A in memory, LVGL's ARGB8888."""
    sym = f"kgui_folder_{key}"
    r, g, b, a = image.split()
    data = Image.merge("RGBA", (b, g, r, a)).tobytes()
    stride = SIZE * 4
    rows = ["    " + ",".join(f"0x{v:02x}" for v in data[i:i + stride]) + "," for i in range(0, len(data), stride)]
    return f'''/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Written by tools/folder_art.py: never by hand. */
#include "lvgl.h"

static const uint8_t {sym}_map[] = {{
''' + "\n".join(rows) + f'''
}};

const lv_image_dsc_t {sym} = {{
  .header = {{
    .magic = LV_IMAGE_HEADER_MAGIC,
    .cf = LV_COLOR_FORMAT_ARGB8888,
    .flags = 0,
    .w = {SIZE},
    .h = {SIZE},
    .stride = {stride},
    .reserved_2 = 0,
  }},
  .data_size = sizeof({sym}_map),
  .data = {sym}_map,
  .reserved = NULL,
}};
'''


def header_text():
    return f'''/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file kgui_folder_art.h
 * @brief The launcher's folder art: Soft Black and a touch of Bold Yellow.
 *
 * Generated by tools/folder_art.py. Do not edit.
 */
#ifndef KGUI_FOLDER_ART_H
#define KGUI_FOLDER_ART_H

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {{
#endif

/**
 * @brief A folder's art.
 *
 * @param key The folder's key in s_art (kgui_group.c), such as `games`.
 * @return Its image, or NULL when there is no folder of that key.
 */
const lv_image_dsc_t *kgui_folder_art(const char *key);

#ifdef __cplusplus
}}
#endif

#endif /* KGUI_FOLDER_ART_H */
'''


def source_text():
    externs = "\n".join(f"extern const lv_image_dsc_t kgui_folder_{k}; /**< {v[1]}. */" for k, v in ART.items())
    rows = "\n".join(f'    {{ "{k}", &kgui_folder_{k} }},' for k in ART)
    return f'''/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file kgui_folder_art.c
 * @brief The folder art by key.
 *
 * Generated by tools/folder_art.py. Do not edit.
 */
#include "kgui_folder_art.h"

#include <stddef.h>
#include <string.h>

{externs}

/** @brief A folder's key and its image. */
typedef struct {{
    const char           *key;   /**< The folder's key in s_art (kgui_group.c). */
    const lv_image_dsc_t *image; /**< Its art. */
}} folder_art_t;

static const folder_art_t s_folders[] = {{
{rows}
}};

const lv_image_dsc_t *kgui_folder_art(const char *key)
{{
    const lv_image_dsc_t *image = NULL;

    if ( NULL != key )
    {{
        for ( size_t i = 0U; (i < (sizeof(s_folders) / sizeof(s_folders[0]))) && (NULL == image); ++i )
        {{
            if ( strcmp(key, s_folders[i].key) == 0 )
            {{
                image = s_folders[i].image;
            }}
        }}
    }}
    return image;
}}
'''


def font(size):
    """The brand's Outfit for the sheet's names when it is there, any font when not."""
    try:
        f = ImageFont.truetype(str(BRAND_FONT), size)
        f.set_variation_by_axes([600])
        return f
    except (OSError, AttributeError):
        return ImageFont.load_default()


def disc_tile(image, scale):
    """A folder on its 80 px disc, at a scale, drawn the way the screen does."""
    d = 80 * scale
    aa = 4
    tile = Image.new("L", (d * aa, d * aa), 0)
    ImageDraw.Draw(tile).ellipse((0, 0, d * aa - 1, d * aa - 1), fill=255)
    out = Image.new("RGBA", (d, d), (0, 0, 0, 255))
    out.paste(Image.new("RGBA", (d, d), DISC + (255,)), (0, 0), tile.resize((d, d), Image.LANCZOS))
    art = image.resize((SIZE * scale, SIZE * scale), Image.NEAREST) if scale > 1 else image
    pos = (d - art.width) // 2
    out.alpha_composite(art, (pos, pos))
    return out


def sheet(images, path):
    """Every folder at real size with its name and, under them, at twice the size."""
    cols = 7
    cell_1, row_1 = 124, 124
    cell_2, row_2 = 184, 200
    pad = 24
    width = pad * 2 + cols * cell_2
    rows = (len(images) + cols - 1) // cols
    height = pad * 2 + rows * row_1 + rows * row_2 + pad
    page = Image.new("RGBA", (width, height), (0, 0, 0, 255))
    draw = ImageDraw.Draw(page)
    small = font(15)
    for i, (key, image) in enumerate(images.items()):
        x = pad + (i % cols) * cell_2 + (cell_2 - cell_1) // 2
        y = pad + (i // cols) * row_1
        page.alpha_composite(disc_tile(image, 1), (x + (cell_1 - 80) // 2, y))
        name = ART[key][0]
        draw.text((x + cell_1 // 2, y + 90), name, font=small, fill=(230, 226, 222, 255), anchor="ma")
        x = pad + (i % cols) * cell_2
        y = pad + rows * row_1 + pad + (i // cols) * row_2
        page.alpha_composite(disc_tile(image, 2), (x + (cell_2 - 160) // 2, y))
        draw.text((x + cell_2 // 2, y + 168), name, font=small, fill=(230, 226, 222, 255), anchor="ma")
    page.convert("RGB").save(path)


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--check", action="store_true")
    ap.add_argument("--sheet", type=Path)
    ap.add_argument("--png", type=Path)
    args = ap.parse_args()
    images = {k: render(v[2]) for k, v in ART.items()}
    jobs = [(OUT / f"kgui_folder_{k}.c", c_image(k, im)) for k, im in images.items()]
    jobs.append((OUT / "kgui_folder_art.h", header_text()))
    jobs.append((OUT / "kgui_folder_art.c", source_text()))
    if args.check:
        stale = [p for p, text in jobs if not p.exists() or p.read_text() != text]
        for p in stale:
            print(f"{p.relative_to(KGUI)} is stale: run tools/folder_art.py")
        return 1 if stale else 0
    for path, text in jobs:
        path.write_text(text)
    if args.png:
        args.png.mkdir(parents=True, exist_ok=True)
        for k, im in images.items():
            im.save(args.png / f"{k}.png")
    if args.sheet:
        sheet(images, args.sheet)
    print(f"wrote {len(images)} folders and kgui_folder_art.{{h,c}}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
