# SPDX-License-Identifier: GPL-3.0-or-later
"""App data folders for kcli: validate app.json, convert an icon, name the path.

The board reads a JSON subset (the app.json keys, in
https://docs.kode.diy/internals/apps-region) and one raw
icon format, 64x64 RGB565 followed by an 8-bit alpha plane. Everything that
needs an image library happens here, on the host: the board never decodes
a picture. Pillow is imported lazily, so `kcli` without Pillow still runs
every verb but `appdata put` with a PNG or JPEG.
"""
import os
import struct

LIMITS = {"name": 31, "category": 63, "version": 15, "description": 52, "icon": 40}
K64_BYTES = 64 * 64 * 2 + 64 * 64


def validate(obj):
    if not isinstance(obj, dict):
        raise ValueError("app.json: not an object")
    if obj.get("kode") != "1":
        raise ValueError("kode: must be \"1\"")
    out = {}
    for k, v in obj.items():
        if any(ord(c) < 0x20 or ord(c) > 0x7E for c in k):
            raise ValueError(f"{k!r}: a key is ASCII only")   # json.dumps would write \uXXXX, which the board refuses
        if not isinstance(v, str):
            raise ValueError(f"{k}: values are strings")
        if k in LIMITS and len(v) > LIMITS[k]:
            raise ValueError(f"{k}: longer than {LIMITS[k]}")
        if k == "color" and not (len(v) == 7 and v[0] == "#" and all(c in "0123456789abcdefABCDEF" for c in v[1:])):
            raise ValueError("color: #RRGGBB")
        if k == "icon" and (v.startswith("/") or "\\" in v or ".." in v.split("/")):
            raise ValueError("icon: relative path, no ..")
        if any(ord(c) < 0x20 or ord(c) > 0x7E for c in v):
            raise ValueError(f"{k}: ASCII only")
        out[k] = v
    return out


def find_icon(d, obj):
    for name in ([obj["icon"]] if "icon" in obj else []) + ["icon.png", "icon.jpg", "icon.jpeg", "icon.k64"]:
        p = os.path.join(d, name)
        if os.path.isfile(p):
            return p
    return None


def to_k64(path):
    if path.lower().endswith(".k64"):
        data = open(path, "rb").read()
        if len(data) != K64_BYTES:
            raise ValueError(f"icon: {os.path.basename(path)} is {len(data)} bytes, "
                             f"a .k64 is {K64_BYTES}")
        return data
    from PIL import Image, ImageOps          # the one dependency, only here
    im = ImageOps.fit(Image.open(path).convert("RGBA"), (64, 64), Image.LANCZOS)
    rgb = bytearray()
    alpha = bytearray()
    for r, g, b, a in im.getdata():
        rgb += struct.pack("<H", ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3))
        alpha.append(a)
    return bytes(rgb + alpha)


def folder(project):
    return f"/storage/.kode/appsdata/{project}"
