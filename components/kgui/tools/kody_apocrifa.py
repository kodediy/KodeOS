#!/usr/bin/env python3
"""Kody's scenes, brought from the apocrifa (kode-dot-os, components/kody_anims), never drawn here.

    tools/kody_apocrifa.py <kode-dot-os checkout> <kody checkout>

Writes, from the animations the apocrifa compiles:

  sdk/components/kui/kody/assets/   the scenes' wake-up, loop and look, and kody.json;
                                    then run the kit's tools/kodygen.py for the pack
  components/kgui/kgui_kody_rest*   what the pack format has no slot for, compiled into
                                    kgui: each scene's <scene>_duerme (falling asleep in
                                    colour) and <scene>_bn (the rest, white outline on
                                    black, decision 0016 of the apocrifa), and <scene>_pk.

<scene>_pk is the one file the apocrifa does not have, because it wakes on one press:
the first press at Rest shows Kody's blue and face in colour with the rest of the scene
still outlined (Luismi, 4-nov). It is the apocrifa's own propuestas/escenas/herramientas/
trazo.py run on <scene>_dormido, leaving the groups `postura` (face, ears, body) and `mano`
(his hand, not what it holds) alone.
The script first runs that same tool unchanged and exits if it does not give the
apocrifa's <scene>_bn back, so the outlines cannot drift from it.

The frame is the apocrifa's (kshell_slot.c): the 410 x 288 scenes at 1.16, 476 x 334 with
their bottom right corner on the glass's, so Kody is cut by the right edge on purpose, on a
canvas of 410 x 334 at y 168; the clock family, drawn for the whole glass, unscaled. And
#131313 to the Dot's true black.
"""
import copy
import importlib.util
import json
import re
import sys
import zlib
from pathlib import Path

ME = Path(__file__).resolve()
KGUI = ME.parents[1]
KIT = KGUI.parent.parent / "sdk" / "components" / "kui"

# The apocrifa's SETS (kshell_face.c): loop name, frames between anchors.
# tren and reloj every 45 f; the calm 270 f loops and the new scenes on f0 and f135.
SCENES = [
    ("tren", "escena_tren", 45), ("parque", "escena_parque", 135), ("camping", "escena_camping", 135),
    ("azotea", "escena_azotea", 135), ("madrugada", "escena_madrugada", 135),
    ("desayuno", "escena_desayuno", 135), ("taller", "escena_taller", 135),
    ("lluvia", "escena_lluvia", 135), ("playa", "escena_playa", 135), ("reloj", "reloj", 45),
]
OUTLINED = [s for s, _, _ in SCENES if s != "reloj"]  # the apocrifa has no reloj_bn
JOKES = ("tren", "parque", "camping", "azotea", "madrugada")  # SETS' .chiste
BLACK = {"#131313": "#000000"}
SLEEP_FROM_PCT = 40   # where <scene>_duerme starts: f32 of 80, the lids falling
SLEEP_FPS = 45        # and half as fast again as it was drawn


def compiled(apo, name):
    """The JSON the apocrifa compiles under a name."""
    src = (apo / "components/kody_anims/anims" / f"kody_{name}.c").read_text()
    raw = bytes(int(x, 16) for x in re.findall(r"0x[0-9a-f]{2}", src.split("{", 1)[1]))
    return zlib.decompress(raw).rstrip(b"\0")


def load_tool(path, name):
    spec = importlib.util.spec_from_file_location(name, path)
    mod = importlib.util.module_from_spec(spec)
    sys.modules[name] = mod
    spec.loader.exec_module(mod)
    return mod


def canon(node):
    if isinstance(node, dict):
        drop = ("nm", "mn", "ix", "cix", "np", "l", "ao", "markers", "meta")
        return {k: (v if k == "c" else canon(v)) for k, v in node.items()
                if k not in drop and not (k == "hd" and not v)}
    if isinstance(node, list):
        return [canon(v) for v in node]
    if isinstance(node, float):
        r = round(node, 2)
        return int(r) if r == int(r) else r
    return node


def dormido(apo, kody, scene):
    """The named source of <scene>_dormido, where trazo.py reads it from."""
    p = apo / "components/kody_anims/extra" / f"kody_{scene}_dormido.json"
    if not p.exists():
        p = kody / "AnimacionesKody/03-lottie" / f"kody_{scene}_dormido.json"
    return json.loads(p.read_text())


def colour_postura(trazo, doc):
    """trazo.pintar_trazo, with Kody's own group kept out of it."""
    kept = []

    def lift(n):
        if isinstance(n, dict):
            if n.get("ty") == "gr" and n.get("nm") in ("postura", "mano"):
                kept.append((n, n["it"]))
                n["it"] = []
                return
            for v in n.values():
                lift(v)
        elif isinstance(n, list):
            for v in n:
                lift(v)

    lift(doc)
    trazo.pintar_trazo(doc)
    for n, it in kept:
        n["it"] = it
    if not kept:
        sys.exit("kody_apocrifa: no group `postura` to keep in colour")


def entry(name, source, anchor=0, family="288"):
    a = {"name": name, "source": source, "w": 410, "h": 334, "anchor": anchor, "recolour": {}}
    if family == "288":
        a.update(scale=1.16, dx=-65.6, dy=0)
    else:
        a.update(scale=1, dx=0, dy=-168)
    return a


def main():
    apo, kody = Path(sys.argv[1]).expanduser(), Path(sys.argv[2]).expanduser()
    sys.path.insert(0, str(KIT / "tools"))
    kodygen = load_tool(KIT / "tools" / "kodygen.py", "kodygen")
    tools = apo / "propuestas/escenas/herramientas"
    sys.path.insert(0, str(tools))
    trazo = load_tool(tools / "trazo.py", "trazo")

    def with_black(a, doc):
        counts = {"#131313": 0}
        kodygen.recolour_items(sum((L.get("shapes", []) for L in copy.deepcopy(doc)["layers"]), []),
                               {"#131313": "#000000"}, counts)
        if counts["#131313"]:
            a["recolour"] = dict(BLACK)
        return a

    # ---- the outlines cannot drift from the apocrifa's
    for s in OUTLINED:
        doc = dormido(apo, kody, s)
        trazo.congelar_kody(doc["layers"][0]["shapes"])
        trazo.pintar_trazo(doc)
        theirs = json.loads(compiled(apo, f"escena_{s}_bn"))
        if json.dumps(canon(doc), sort_keys=True) != json.dumps(canon(theirs), sort_keys=True):
            sys.exit(f"kody_apocrifa: trazo.py does not give the apocrifa's escena_{s}_bn back")
    print("trazo.py gives back the apocrifa's nine outlines, byte for byte once minified")

    # ---- the pack's sources
    assets = KIT / "kody" / "assets"
    manifest = json.loads((assets / "kody.json").read_text())
    for old in ("escena_mazo", "despierta_mazo"):
        (assets / f"kody_{old}.json").unlink(missing_ok=True)
    keep_roles = {a["name"]: a for a in manifest["animations"]
                  if a["name"] in ("duda", "portatil", "despide", "cargando")}
    animations, scenes = [], []
    for s, loop, anchor in SCENES:
        family = "502" if s == "reloj" else "288"
        on = {"look": f"{s}_te_mira", "hour": f"{s}_mira_hora", "notice": f"{s}_lee_aviso",
              "plug": f"{s}_enchufado", "joke": f"{s}_chiste"}
        if s not in JOKES:
            del on["joke"]
        files = [(loop, anchor), (f"despierta_{s}", 0)] + [(n, 0) for n in on.values()]
        for name, a_ in files:
            raw = compiled(apo, name)
            (assets / f"kody_{name}.json").write_bytes(raw + b"\n")
            a = with_black(entry(name, f"kody_{name}.json", a_, family), json.loads(raw))
            if s == "reloj":
                a["builtin"] = True
            a["help"] = f"From the apocrifa's kody_anims: {name}."
            animations.append(a)
        scenes.append({"name": s, "wake": f"despierta_{s}", "loop": loop, "on": {t: [n] for t, n in on.items()}})
    # The roles outside the face: portatil is the apocrifa's, duda is approved in the kody repo
    # and the apocrifa does not compile it. despide and cargando are still `borrador` in the
    # kody repo's estados.json, which only Luismi promotes: they stay as they are.
    (assets / "kody_portatil.json").write_bytes(compiled(apo, "portatil") + b"\n")
    (assets / "kody_duda.json").write_bytes((kody / "AnimacionesKody/03-lottie/kody_duda.json").read_bytes())
    animations += list(keep_roles.values())
    manifest["animations"] = animations
    pack = manifest["pack"]
    pack["version"] = "2026.11.05.2"
    pack["scenes"] = scenes
    # The hour's slots have several scenes each, which the format has not: kgui draws them
    # (gui_scene_of_the_hour()), as the apocrifa does; this is the floor with no hour read.
    pack["face"] = {"default": "tren", "hours": []}
    (assets / "kody.json").write_text(json.dumps(manifest, indent=2, ensure_ascii=False) + "\n")

    # ---- what kgui compiles
    blobs, table = [], []
    for s, _, _ in SCENES:
        family = "502" if s == "reloj" else "288"
        row = {"sleep": f"{s}_duerme", "rest": None, "peek": None}
        # Luismi, 5-nov: at the press he is already closing his eyes, without the stretch
        # that opens the animation (f0 to f26 of its 80), and quicker.
        duerme = json.loads(compiled(apo, f"{s}_duerme"))
        duerme["ip"] = (duerme["op"] * SLEEP_FROM_PCT) // 100
        duerme["fr"] = SLEEP_FPS
        docs = [("sleep", f"{s}_duerme", duerme)]
        if s in OUTLINED:
            row["rest"], row["peek"] = f"{s}_bn", f"{s}_pk"
            docs.append(("rest", f"{s}_bn", json.loads(compiled(apo, f"escena_{s}_bn"))))
            pk = dormido(apo, kody, s)
            trazo.congelar_kody(pk["layers"][0]["shapes"])
            colour_postura(trazo, pk)
            docs.append(("peek", f"{s}_pk", pk))
        for _, name, doc in docs:
            found = kodygen.lint(name, doc)
            if found:
                sys.exit("\n".join(found))
            a = with_black(entry(name, name, 0, family), doc)
            blob, fps, frames, _ = kodygen.prepare(a, doc)
            blobs.append((name, zlib.compress(blob, 9), len(blob), frames, fps))
        table.append((s, row))

    out = ["/* SPDX-License-Identifier: GPL-3.0-or-later */",
           "/* Written by tools/kody_apocrifa.py from the apocrifa's kody_anims: never by hand. */",
           '#include "kgui_kody_rest.h"', "", "#include <string.h>", ""]
    for name, z, json_len, frames, fps in blobs:
        out.append(f"static const uint8_t z_{name}[{len(z)}] = {{")
        out += ["    " + ", ".join(f"0x{b:02x}" for b in z[i:i + 16]) + "," for i in range(0, len(z), 16)]
        out += ["};", f"static const kui_kody_anim_t a_{name} = {{",
                f"    .zlib = z_{name}, .zlib_len = {len(z)}U, .json_len = {json_len}U,",
                f"    .w = 410U, .h = 334U, .frames = {frames}U, .fps = {fps}U, .anchor = 0U, .deflated = true,",
                "};", ""]
    out.append("static const struct { const char *scene; kgui_kody_rest_t rest; } s_scenes[] = {")
    for s, r in table:
        ref = lambda k: f"&a_{r[k]}" if r[k] else "NULL"
        out.append(f'    {{ "{s}", {{ {ref("sleep")}, {ref("rest")}, {ref("peek")} }} }},')
    out += ["};", "",
            "const kgui_kody_rest_t *kgui_kody_rest_of(const char *scene)", "{",
            "    const kgui_kody_rest_t *r = NULL;", "",
            "    for ( size_t i = 0U; (NULL != scene) && (NULL == r) && (i < (sizeof(s_scenes) / sizeof(s_scenes[0]))); ++i )",
            "    {",
            "        if ( strcmp(s_scenes[i].scene, scene) == 0 )", "        {", "            r = &s_scenes[i].rest;", "        }",
            "    }", "    return r;", "}", ""]
    (KGUI / "kgui_kody_rest_data.c").write_text("\n".join(out))
    total = sum(len(z) for _, z, _, _, _ in blobs)
    print(f"{len(blobs)} animations compiled into kgui, {total // 1024} KB of flash")
    for name, z, json_len, frames, _ in blobs:
        print(f"  {name:18} {frames:4} f  {json_len // 1024:3} KB  zlib {len(z)}")


if __name__ == "__main__":
    main()
