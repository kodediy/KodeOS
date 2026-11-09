#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""The station's simple reports, to be read without knowing anything of the test.

Reads results.json in every run folder of the reports directory and writes,
without touching report.html:
  <reports>/index.html             every run, the newest first
  <reports>/<folder>/simple.html   one run: the verdict, what fails in plain
                                   words, every picture
The pictures go inside the HTML (data: URIs), so a report opens the same from
the Finder, from test_dot_ui.py or as an attachment.

    python3 tools/factory/ui/reports.py [--dir ~/kode-factory/reports] [--folder <date>_<serial>] [--all]

Standard library only. test_dot_ui.py imports it to serve /reports/ and to
write a run's page when the run ends.
"""

import argparse
import base64
import html
import json
import re
import time
from pathlib import Path

REPORTS = Path("~/kode-factory/reports").expanduser()
E = html.escape

PASS, FAIL, NOVERDICT, NOTRUN = "PASS", "FAIL", "NO VERDICT", "NOT RUN"

# The operator's words for each test. The screen ones come from the station's
# camera looking at the Kode's glass.
NAME = {
    "info": "Identification", "place": "Placement", "pmic": "Charger", "gauge": "Battery (gauge)",
    "ioexp": "Input expander", "clocks": "Clock", "imu": "Motion sensor", "mag": "Compass",
    "haptic": "Vibration", "audio": "Audio", "sd": "microSD card", "nfc": "NFC", "rfid": "RFID",
    "ir": "Infrared", "c5": "Wi-Fi chip", "wifi_24": "Wi-Fi 2.4 GHz", "wifi_5": "Wi-Fi 5 GHz", "ble_rx": "Bluetooth",
    "cam_framing": "Screen: not seen whole", "cam_pixels": "Screen: pixels", "cam_lines": "Screen: dead line",
    "cam_zones": "Screen: dark zone", "cam_colour": "Screen: colour", "cam_straight": "Screen crooked",
    "led": "LED", "pads_charge": "Rear pads: charging", "pads_delivery": "Rear pads: delivery",
    "pads_gnd": "Rear pads: GND", "pads_loop": "Rear pads: data", "buttons": "Buttons", "tp": "Touch",
    "mem": "Memory", "exit_record": "Exit record", "certificate": "Certificate", "factory_reset": "Factory reset",
    "lock": "Not locked: do not ship this Kode",
}
BUTTON = {"left": "LEFT", "right": "RIGHT", "up": "UP", "down": "DOWN", "top": "BLUE", "bottom": "RED"}

# Failures of the station, not of the Kode, read from the reasons
# test_dot_final.py writes into results.json; page.html keeps the same rules.
# Each rule: (test, reason, plain words).
STATION = [
    (re.compile(r"^pads_"), re.compile(r"^station: "), "Rear pads: the station's relays or its 5 V"),
    (re.compile(r"^nfc$"), re.compile(r"the write found no tag"), "NFC: the station's test tag was not read"),
]
NOT_JUDGED = {"exit_record"}          # the exit record never moves the verdict
REPEATED_S = 15 * 60                  # two runs of one serial closer than this: the test was repeated, the last counts


def base_verdict(v):
    return (v or "").replace("BENCH ", "")


def reason(p):
    """The reason test_dot_final.py sends in its result event: a station's
    own fault as measured, else the last `!E` line of the detail, else what
    was measured."""
    measured = p.get("measured") or ""
    if measured.startswith("station: "):
        return measured.strip()
    errs = re.findall(r"!E [^ ]+ [^ ]+ (.*)", p.get("detail") or "")
    return (errs[-1] if errs else measured).strip()


def buttons(p):
    """One phrase per button from the row's `step=N button=X result=Y` steps."""
    txt = (p.get("measured") or "") + "\n" + (p.get("detail") or "")
    by, last, double = {}, {}, set()
    for m in re.finditer(r"step=(\d+) button=(\S+) result=(\S+)", txt):
        k, r = m.group(1), m.group(3)
        b = BUTTON.get(m.group(2).replace("pad-", ""), m.group(2).upper())
        if r == "double":
            double.add(k)
        last[k] = r
        r = {"stuck": "held"}.get(r, r)
        if r in ("timeout", "held", "fail") and b not in by.setdefault(r, []):
            by[r].append(b)
    double = any(last.get(k) != "pass" for k in double)     # a double press later repeated well is no fault
    out = [f"Button {b} stuck down" for b in by.get("held", [])]
    out += [f"Button {b} fails" for b in by.get("fail", [])]
    t = by.get("timeout", [])
    if len(t) >= 6:
        out.append("No button pressed in time")
    elif t:
        out.append(("Button " if len(t) == 1 else "Buttons ") + ", ".join(t) + " not pressed in time")
    if double:
        out.append("The pad presses two buttons at once")
    return out or ["Buttons"]


def phrases(p):
    """What fails, in the operator's words: (phrases, kind), kind one of
    kode | station | unmeasured | notrun."""
    i, st, why = p.get("id", ""), p.get("status", ""), reason(p)
    for rid, rwhy, words in STATION:
        if rid.search(i) and rwhy.search(why + " " + (p.get("measured") or "")):
            return [words], "station"
    if st == NOTRUN:
        return [NAME.get(i, p.get("name", i))], "notrun"
    if i == "place":
        w = ("Kode not connected on USB" if "USB" in why else
             "The station's camera does not answer (J3 connector)" if "J3" in why else
             "The screen does not answer" if "panel" in why else
             "Kode badly seated in the fixture" if "seated" in why else "Placement")
        return [w], ("unmeasured" if st == NOVERDICT else "kode")
    if st == NOVERDICT:
        return [NAME.get(i, p.get("name", i)) + " (could not be measured)"], "unmeasured"
    if i == "audio":
        return ["Microphone" if re.search(r"sample|ADC", why) else "Audio"], "kode"
    if i == "cam_pixels":
        t = why + " " + (p.get("measured") or "")
        dead, on = re.search(r"dead=(\d+)", t), re.search(r"stuck=(\d+)", t)
        out = []
        if dead and int(dead.group(1)):
            out.append("Screen: dead pixel" + (f" ({dead.group(1)})" if int(dead.group(1)) > 1 else ""))
        if (on and int(on.group(1))) or "at=on:" in t:
            out.append("Screen: pixel lit on black")
        return out or ["Screen: pixels"], "kode"
    if i == "buttons":
        return buttons(p), "kode"
    if i == "tp":
        return ["Touch: did not answer in time" if st == "TIMEOUT" else "Touch"], "kode"
    return [NAME.get(i, p.get("name", i))], "kode"


def read(folder):
    d = Path(folder)
    try:
        j = json.loads((d / "results.json").read_text())
    except (OSError, ValueError):
        return None
    steps = j.get("steps") or {}
    steps = [p for p in (steps.values() if isinstance(steps, dict) else steps) if isinstance(p, dict)]
    kode, station, unmeasured, notrun = [], [], [], 0
    for p in steps:
        if p.get("status") == PASS or p.get("id") in NOT_JUDGED:
            continue
        fs, kind = phrases(p)
        if kind == "notrun":
            notrun += 1
            continue
        dst = {"kode": kode, "station": station, "unmeasured": unmeasured}[kind]
        for f in fs:
            if f not in dst:
                dst.append(f)
    ver = j.get("verdict") or ""
    if base_verdict(ver) not in (PASS, FAIL, NOVERDICT):
        ver = FAIL if kode or station else (NOVERDICT if unmeasured or notrun else PASS)
    date, _, serial = d.name.partition("_")
    info = j.get("info") or {}
    return dict(folder=d.name, path=d, date=date, serial=info.get("serial") or serial, verdict=ver, kode=kode,
                station=station, unmeasured=unmeasured, notrun=notrun, steps=steps, info=info,
                total=j.get("total_s"), counts=None, earlier=0)


def seconds(date):
    try:
        return time.mktime(time.strptime(date[:15], "%Y%m%d-%H%M%S"))
    except ValueError:
        return None


def every(base=REPORTS):
    """Every run, the newest first. When a Kode was repeated straight away
    (a failed screen is repeated without taking it out), the earlier run's
    `counts` names the folder that counts."""
    base = Path(base)
    if not base.is_dir():
        return []
    rs = [r for r in (read(d) for d in sorted(base.iterdir(), reverse=True)
                      if d.is_dir() and (d / "results.json").exists()) if r]
    for new, old in zip(rs, rs[1:]):
        a, b = seconds(new["date"]), seconds(old["date"])
        if new["serial"] == old["serial"] and new["serial"] not in ("-", "") and a and b and 0 <= a - b <= REPEATED_S:
            old["counts"] = new["counts"] or new["folder"]
    for r in rs:
        if r["counts"]:
            for x in rs:
                if x["folder"] == r["counts"]:
                    x["earlier"] += 1
    return rs


def when(date):
    if len(date) >= 15:
        return f"{date[6:8]}/{date[4:6]}/{date[:4]}", f"{date[9:11]}:{date[11:13]}:{date[13:15]}"
    return date, ""


COLOURS = {PASS: ("#1f9d4c", "#fff"), FAIL: ("#d42f37", "#fff"), NOVERDICT: ("#f07a12", "#231000")}
CSS = """:root{--tx:#151517;--tx2:#5f5f68;--ln:#e2e2e6;--bg:#f6f6f8;--pass:#1f9d4c;--fail:#d42f37;--nov:#f07a12;--station:#7a7a84}
*{box-sizing:border-box}body{margin:0;background:var(--bg);color:var(--tx);font:18px/1.35 -apple-system,system-ui,"Helvetica Neue",Arial,sans-serif}
a{color:inherit}.w{max-width:1180px;margin:0 auto;padding:24px 20px 60px}
.chip{display:inline-block;padding:.12em .55em;border-radius:8px;font-weight:700;margin:2px 6px 2px 0;font-size:17px}
.ck{background:#fde5e6;color:#a1161d}.cb{background:#ececef;color:#55555e}.cn{background:#fff0e0;color:#9a4a00}
.cb::before{content:"Station · ";font-weight:600;opacity:.8}.cg{background:#ececef;color:#55555e;font-weight:600}
@media (prefers-color-scheme:dark){:root{--tx:#f1f1f3;--tx2:#a3a3ad;--ln:#2e2e34;--bg:#111114}
 .ck{background:#4a1a1d;color:#ffb3b7}.cb,.cg{background:#26262c;color:#a8a8b2}.cn{background:#45290f;color:#ffc58f}}
"""
LOGO = '<svg viewBox="0 0 24 24" width="1em" height="1em"><circle cx="12" cy="12" r="10.5" fill="none" stroke="currentColor" stroke-width="2"/>{}</svg>'
ICON = {PASS: '<path d="M7 12.5l3.2 3.2L17 9" fill="none" stroke="currentColor" stroke-width="2.6" stroke-linecap="round" stroke-linejoin="round"/>',
        FAIL: '<path d="M8 8l8 8M16 8l-8 8" stroke="currentColor" stroke-width="2.6" stroke-linecap="round"/>',
        NOVERDICT: '<path d="M12 6.5v7M12 17v.5" stroke="currentColor" stroke-width="2.8" stroke-linecap="round"/>'}


def chips(r):
    h = "".join(f'<span class="chip ck">{E(x)}</span>' for x in r["kode"])
    h += "".join(f'<span class="chip cn">{E(x)}</span>' for x in r["unmeasured"])
    h += "".join(f'<span class="chip cb">{E(x)}</span>' for x in r["station"])
    if r["notrun"]:
        h += f'<span class="chip cg">{r["notrun"]} tests not done</span>'
    return h


def list_page(base=REPORTS, href=lambda f: f"{f}/simple.html", rs=None):
    rs = every(base) if rs is None else rs
    rows, day = [], None
    for r in rs:
        d, h = when(r["date"])
        if d != day:
            day = d
            rows.append(f"<h2>{E(d)}</h2>")
        v = base_verdict(r["verdict"])
        bg, fg = COLOURS[v]
        note = '<div class="only">only the station</div>' if v == FAIL and not r["kode"] and r["station"] else ""
        if r["counts"]:
            note += '<div class="only">earlier: does not count</div>'
        elif r["earlier"]:
            note += '<div class="only">repeated · this one counts</div>'
        cls = (" old" if r["counts"] else "") + (" counts" if r["earlier"] else "")
        rows.append(f'''<a class="run{cls}" href="{E(href(r["folder"]))}"><div class="v{' nv' if v == NOVERDICT else ''}" style="background:{bg};color:{fg}">{LOGO.format(ICON[v])}<b>{E(r["verdict"])}</b>{note}</div>
<div class="q"><div class="h">{E(h)}</div><div class="s">{E(r["serial"])}</div></div><div class="f">{chips(r) or '<span class="ok">All good</span>'}</div></a>''')
    return f"""<!doctype html><html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Station runs</title><style>{CSS}
h1{{font-size:28px;margin:0 0 6px}}.sub{{color:var(--tx2);margin:0 0 10px}}h2{{font-size:16px;color:var(--tx2);text-transform:uppercase;letter-spacing:.08em;margin:26px 0 8px}}
.run{{display:grid;grid-template-columns:270px 150px 1fr;gap:18px;align-items:center;background:color-mix(in srgb,var(--bg) 40%,#fff);border:1px solid var(--ln);border-radius:16px;padding:10px;margin-bottom:10px;text-decoration:none}}
@media (prefers-color-scheme:dark){{.run{{background:#1a1a1f}}}}
.run:hover{{border-color:var(--tx2)}}.run.old{{opacity:.45;margin:-4px 0 10px 40px;transform:scale(.97);transform-origin:left}}.run.counts{{border:3px solid var(--tx);margin-bottom:4px}}
.v{{border-radius:12px;padding:14px 16px;display:flex;align-items:center;gap:12px;font-size:30px;flex-wrap:wrap;min-height:76px}}.v b{{font-weight:900;letter-spacing:-.01em;font-size:30px;line-height:1;white-space:nowrap}}.v.nv b{{font-size:24px}}
.v svg{{flex:none;width:34px;height:34px}}.only{{font-size:13px;font-weight:700;width:100%;opacity:.9}}
.h{{font-size:30px;font-weight:800;font-variant-numeric:tabular-nums}}.s{{font:13px ui-monospace,Menlo,monospace;color:var(--tx2)}}.ok{{color:var(--pass);font-weight:700}}
@media (max-width:760px){{.run{{grid-template-columns:1fr}}}}
</style></head><body><div class="w"><h1>Final test · runs</h1><p class="sub">The newest at the top. Open one to see what fails and its pictures. Grey = a fault of the station, not of the Kode.</p>
{''.join(rows) or '<p>No runs yet.</p>'}</div></body></html>"""


def caption(name):
    """One line under each picture."""
    n = name.lower()
    m = re.search(r"img_cam_(dead|on|stuck)_(\d+)_(\d+)", n)
    if m:
        return ("Dead pixel" if m.group(1) == "dead" else "Pixel lit on black") + f", enlarged (at x {m.group(2)}, y {m.group(3)})"
    for k, t in (("location", "What the camera sees when the Kode is placed: the four marks"),
                 ("colour_strip", "Screen colour"), ("colours", "The screen in red, green, blue, white and grey"),
                 ("zone_", "Screen: dark zones"), ("photo_", "Photo of the run: " + name)):
        if k in n:
            return t
    return "Picture: " + name


def b64(f):
    t = {".png": "image/png", ".jpg": "image/jpeg", ".jpeg": "image/jpeg"}[f.suffix.lower()]
    return f"data:{t};base64," + base64.b64encode(f.read_bytes()).decode()


def run_page(r, list_href="../index.html", tech_href="report.html"):
    d = r["path"]
    day, h = when(r["date"])
    v = base_verdict(r["verdict"])
    bg, fg = COLOURS[v]
    counts = r.get("counts")
    note = (f'<a class="nocount" href="../{E(counts)}/simple.html">This run does not count: this Kode was repeated at '
            f'{E(when(counts.partition("_")[0])[1])}. See the one that counts ›</a>') if counts else ""
    refs = {}
    for p in r["steps"]:
        for im in p.get("images") or []:
            refs.setdefault(Path(im).name, []).append(p)
    bad = {n for n, ps in refs.items() if any(p.get("status") != PASS and phrases(p)[1] == "kode" for p in ps)}
    imgs = sorted([x for x in d.iterdir() if x.suffix.lower() in (".png", ".jpg", ".jpeg") and x.is_file()
                   and x.stat().st_size < 6_000_000],
                  key=lambda x: (x.name not in bad, not x.name.startswith("img_"), x.name))
    figs = []
    for x in imgs:
        is_bad, small = x.name in bad, bool(re.search(r"img_cam_(dead|on|stuck)_", x.name))
        figs.append(f'<figure class="{"bad" if is_bad else ""}{" small" if small else ""}"><div class="im"><img src="{b64(x)}" alt="">'
                    f'{"<i class=ring></i>" if small else ""}</div><figcaption>{"<b>Fails · </b>" if is_bad else ""}'
                    f'{E(caption(x.name))}</figcaption></figure>')
    body = ""
    if r["kode"]:
        body += '<h2>What fails</h2><ul class="k">' + "".join(f"<li>{E(x)}</li>" for x in r["kode"]) + "</ul>"
    if r["unmeasured"]:
        body += (f'<h2>{"Why there is no verdict" if v == NOVERDICT else "Could not be measured"}</h2><ul class="n">'
                 + "".join(f"<li>{E(x)}</li>" for x in r["unmeasured"]) + "</ul>")
    if r["station"]:
        body += ('<h2>Station <small>(a fault of the station, not of the Kode)</small></h2><ul class="b">'
                 + "".join(f"<li>{E(x)}</li>" for x in r["station"]) + "</ul>")
    if r["notrun"]:
        body += f'<p class="np">{r["notrun"]} tests were not done.</p>'
    if not body:
        body = '<p class="all">All good.</p>'
    only = ('<div class="only">The Kode fails nothing: the FAIL is the station alone.</div>'
            if v == FAIL and not r["kode"] and r["station"] else "")
    det = "".join(f'<tr class="s-{E(p.get("status", "").replace(" ", "_"))}"><td>{E(p.get("name", ""))}</td><td><b>{E(p.get("status", ""))}</b></td>'
                  f'<td>{E(str(p.get("measured", ""))[:400])}</td><td>{E(str(p.get("range", "")))}</td><td>{p.get("secs", "")}</td></tr>'
                  for p in r["steps"])
    inf = r["info"]
    meta = " · ".join(E(str(x)) for x in (f"kodeOS {inf.get('kodeos', '')}", f"C5 {inf.get('c5', '')}",
                                         f"MAC {inf.get('mac', '')}", f"{r['total']} s" if r["total"] is not None else "") if x)
    tech = f'<a href="{E(tech_href)}">Full technical report</a>' if (d / "report.html").exists() else ""
    return f"""<!doctype html><html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>{E(r["verdict"])} {E(h)} · station</title><style>{CSS}
.top{{display:flex;justify-content:space-between;align-items:center;margin-bottom:14px}}.top a{{font-weight:700;text-decoration:none;color:var(--tx2)}}
.ver{{background:{bg};color:{fg};border-radius:22px;padding:26px 30px;display:flex;align-items:center;gap:26px;flex-wrap:wrap}}
.ver svg{{width:110px;height:110px}}.ver b{{font-size:{110 if len(r["verdict"]) < 6 else 72}px;line-height:.9;font-weight:900;letter-spacing:-.02em}}
.ver .q{{margin-left:auto;text-align:right}}.ver .q div:first-child{{font-size:34px;font-weight:800;font-variant-numeric:tabular-nums}}.ver .q div{{font-size:17px;opacity:.9}}
.only{{margin-top:10px;font-size:20px;font-weight:700;color:var(--tx2)}}
.nocount{{display:block;background:#26262c;color:#fff;border-radius:14px;padding:14px 20px;margin-bottom:12px;font-size:22px;font-weight:800;text-decoration:none}}
h2{{font-size:24px;margin:28px 0 8px}}h2 small{{font-size:16px;color:var(--tx2);font-weight:600}}
ul{{list-style:none;padding:0;margin:0}}li{{font-size:30px;font-weight:800;padding:6px 0 6px 46px;position:relative}}
li::before{{content:"";position:absolute;left:4px;top:50%;width:26px;height:26px;margin-top:-13px;border-radius:50%}}
.k li::before{{background:var(--fail)}}.n li::before{{background:var(--nov)}}.b li{{color:var(--station);font-size:22px;font-weight:700}}.b li::before{{background:var(--station);width:18px;height:18px;margin-top:-9px;left:8px}}
.all{{font-size:30px;font-weight:800;color:var(--pass)}}.np{{color:var(--tx2);font-size:18px}}
.photos{{display:grid;grid-template-columns:repeat(auto-fill,minmax(min(420px,100%),1fr));gap:18px}}
figure{{margin:0;background:#0d0d10;border-radius:14px;overflow:hidden;border:3px solid transparent}}figure.bad{{border-color:var(--fail)}}
.im{{position:relative;display:flex;align-items:center;justify-content:center;min-height:200px;background:#000}}
.im img{{display:block;width:100%;height:auto;max-height:520px;object-fit:contain;cursor:zoom-in}}figure.small .im img{{width:320px;height:320px;image-rendering:pixelated}}
.ring{{position:absolute;left:50%;top:50%;width:70px;height:70px;margin:-35px 0 0 -35px;border:4px solid #ff3b47;border-radius:50%;pointer-events:none}}
figcaption{{color:#e8e8ec;padding:10px 14px;font-size:17px}}figcaption b{{color:#ff8a90}}
details{{margin-top:30px;background:color-mix(in srgb,var(--bg) 50%,#fff);border:1px solid var(--ln);border-radius:12px;padding:10px 16px}}
@media (prefers-color-scheme:dark){{details{{background:#1a1a1f}}}}
summary{{font-weight:800;cursor:pointer;font-size:18px}}table{{border-collapse:collapse;width:100%;font-size:13px;margin-top:10px}}
td{{border-top:1px solid var(--ln);padding:4px 6px;vertical-align:top;word-break:break-word}}.s-PASS b{{color:var(--pass)}}.s-FAIL b,.s-TIMEOUT b{{color:var(--fail)}}.s-NO_VERDICT b{{color:var(--nov)}}
.meta{{color:var(--tx2);font-size:14px;margin:8px 0}}
#lb{{display:none;position:fixed;inset:0;background:rgba(0,0,0,.92);align-items:center;justify-content:center;cursor:zoom-out}}#lb img{{max-width:96vw;max-height:94vh}}
</style></head><body><div class="w">
<div class="top"><a href="{E(list_href)}">‹ Every run</a>{tech}</div>
{note}<div class="ver">{LOGO.format(ICON[v])}<b>{E(r["verdict"])}</b><div class="q"><div>{E(h)}</div><div>{E(day)}</div><div>{E(r["serial"])}</div></div></div>{only}
{body}
{'<h2>Pictures</h2><div class="photos">' + "".join(figs) + "</div>" if figs else ""}
<details><summary>Details</summary><p class="meta">{meta}</p><table>{det}</table></details>
</div><div id="lb"><img alt=""></div><script>
const lb=document.getElementById("lb"),li=lb.querySelector("img");
document.querySelectorAll(".im img").forEach(i=>i.onclick=()=>{{li.src=i.src;li.style.imageRendering=i.closest(".small")?"pixelated":"auto";lb.style.display="flex"}});
lb.onclick=()=>lb.style.display="none";document.onkeydown=e=>{{if(e.key==="Escape")lb.style.display="none"}};
</script></body></html>"""


def write(base=REPORTS, folder=None, everything=False):
    """Write simple.html for every new or changed run (or for `folder` and
    the same Kode's earlier run, or for all with everything=True), and
    index.html. Returns how many simple.html it wrote."""
    base = Path(base).expanduser()
    base.mkdir(parents=True, exist_ok=True)
    n = 0
    rs = every(base)
    asked = None
    if folder:
        asked = {folder} | {r["folder"] for r in rs if r["counts"] == folder}
    for r in rs:
        d = r["path"]
        dst = d / "simple.html"
        stale = not dst.exists() or dst.stat().st_mtime < (d / "results.json").stat().st_mtime
        if everything or (asked is not None and r["folder"] in asked) or (asked is None and stale):
            dst.write_text(run_page(r))
            n += 1
    (base / "index.html").write_text(list_page(base, rs=rs))
    return n


if __name__ == "__main__":
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--dir", default=str(REPORTS))
    ap.add_argument("--folder")
    ap.add_argument("--all", action="store_true", help="write every simple.html again")
    a = ap.parse_args()
    n = write(a.dir, a.folder, everything=a.all)
    print(f"{n} simple reports written; the list in {Path(a.dir).expanduser() / 'index.html'}")
