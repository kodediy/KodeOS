#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""App data folders: validate app.json, convert an icon, write what the board stores.

The board reads a JSON subset (the app.json keys, in
https://docs.kode.diy/internals/apps-region) and one raw
icon format, 64x64 RGB565 followed by an 8-bit alpha plane. Everything that
needs an image library happens here, on the host: the board never decodes
a picture. Pillow is imported lazily, so `kcli` without Pillow still runs
every verb but `appdata put` with a PNG or JPEG.

An app's metadata travels apart from its image, as a folder. In the app's
repository it is `appdata/`, with `app.json` and an `icon.png`. On the board it
is `/storage/.kode/appsdata/<project>/`, with `app.json` and `icon.k64`.
load() turns the first into the second, and every carrier stores those same
bytes: `kcli.py appdata put` over the cable, the factory's FAT image, a
release's files.

    tools/kappdata.py build SRC OUT    write OUT, the board's folder, from an
                                       app's appdata/ folder SRC; prints its digest
    tools/kappdata.py digest DIR       print the digest of a board folder DIR

Both exit 1, saying why, on a folder the board would not read.
"""
import argparse
import hashlib
import json
import os
import struct
import sys

LIMITS = {"name": 31, "category": 63, "version": 15, "description": 52, "icon": 40}
K64_BYTES = 64 * 64 * 2 + 64 * 64
JSON_MAX = 1024             # KAPP_DATA_JSON_MAX: the longest app.json the board reads
JSON_NAME = "app.json"      # the board folder's two files
ICON_NAME = "icon.k64"


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
        with open(path, "rb") as f:
            data = f.read()
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


def load(src):
    """An app's appdata/ folder as the board stores it: (app.json bytes, icon).

    app.json is checked and written compact, an icon found is converted and
    named icon.k64 in it. Raises ValueError naming the field that the board
    would refuse. The icon comes back empty when the folder has none.
    """
    path = os.path.join(src, JSON_NAME)
    try:
        with open(path, "rb") as fh:
            obj = validate(json.loads(fh.read()))
    except OSError as exc:
        raise ValueError(f"cannot read {path}: {exc}") from exc
    except json.JSONDecodeError as exc:
        raise ValueError(f"{path}: {exc}") from exc

    icon = b""
    icon_path = find_icon(src, obj)
    if icon_path:
        try:
            icon = to_k64(icon_path)
        except ImportError as exc:
            raise ValueError(f"icon: {icon_path} needs Pillow "
                             "(python3 -m pip install pillow)") from exc
        except OSError as exc:      # Pillow's "cannot identify image file"
            raise ValueError(f"icon: {icon_path}: {exc}") from exc
        obj["icon"] = ICON_NAME     # the board reads what is stored
    elif "icon" in obj:
        raise ValueError(f"icon: {obj['icon']} is not in {src}")
    body = json.dumps(obj, separators=(",", ":")).encode("ascii")
    if len(body) > JSON_MAX:
        raise ValueError(f"app.json: {len(body)} bytes once written, "
                         f"longer than {JSON_MAX}")
    return body, icon


def write(out, appdata):
    """Leave in `out` the board's folder: app.json, and icon.k64 when there is one.

    Anything else that was in the folder goes: a pack is built from it, and a
    file of an older build would travel to every board.
    """
    body, icon = appdata
    os.makedirs(out, exist_ok=True)
    for old in os.listdir(out):
        os.remove(os.path.join(out, old))
    with open(os.path.join(out, JSON_NAME), "wb") as fh:
        fh.write(body)
    if icon:
        with open(os.path.join(out, ICON_NAME), "wb") as fh:
            fh.write(icon)


def read(folder_path):
    """A board folder back, as (app.json bytes, icon): the bytes, not rewritten.

    Raises ValueError for a folder the board would not read as it is: a file
    that is neither of the two, no app.json or one the board refuses, an icon
    of another size, an icon and an app.json that do not name each other.
    """
    try:
        names = sorted(os.listdir(folder_path))
    except OSError as exc:
        raise ValueError(f"cannot read {folder_path}: {exc}") from exc
    extra = [n for n in names if n not in (JSON_NAME, ICON_NAME)]
    if extra:
        raise ValueError(f"{folder_path} holds more than {JSON_NAME} and "
                         f"{ICON_NAME}: {', '.join(extra)}")
    if JSON_NAME not in names:
        raise ValueError(f"{folder_path} holds no {JSON_NAME}")
    with open(os.path.join(folder_path, JSON_NAME), "rb") as fh:
        body = fh.read()
    if len(body) > JSON_MAX:
        raise ValueError(f"app.json: {len(body)} bytes, longer than {JSON_MAX}")
    try:
        obj = validate(json.loads(body))
    except json.JSONDecodeError as exc:
        raise ValueError(f"app.json: {exc}") from exc
    icon = b""
    if ICON_NAME in names:
        icon = to_k64(os.path.join(folder_path, ICON_NAME))
        if obj.get("icon") != ICON_NAME:
            raise ValueError(f"icon: {ICON_NAME} is there and app.json does not name it")
    elif "icon" in obj:
        raise ValueError(f"icon: app.json names {obj['icon']}, which is not there")
    return body, icon


def digest(appdata):
    """One SHA-256 for a folder: of app.json's bytes followed by the icon's."""
    body, icon = appdata
    return hashlib.sha256(body + icon).hexdigest()


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    sub = ap.add_subparsers(dest="verb", required=True)
    p = sub.add_parser("build", help="write the board's folder from an app's appdata/")
    p.add_argument("src", help="the app's appdata/ folder: app.json and an icon")
    p.add_argument("out", help="the folder to write: app.json and icon.k64")
    p = sub.add_parser("digest", help="print the digest of a board folder")
    p.add_argument("dir", help="a folder holding app.json and maybe icon.k64")
    a = ap.parse_args()

    try:
        if a.verb == "build":
            appdata = load(a.src)
            write(a.out, appdata)
        else:
            appdata = read(a.dir)
    except (OSError, ValueError) as exc:
        print(f"kappdata: {exc}", file=sys.stderr)
        return 1
    print(digest(appdata))
    return 0


if __name__ == "__main__":
    sys.exit(main())
