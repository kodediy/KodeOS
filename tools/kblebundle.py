#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""kblebundle - the macOS scaffolding that lets a plain script touch
Bluetooth at all.

CoreBluetooth refuses any process whose Info.plist carries no
NSBluetoothAlwaysUsageDescription, and it refuses it by aborting with
SIGABRT and no message - no Python on a Mac declares that key, so bleak
cannot run from a terminal, ever. The fix does not depend on which
kodeOS tool needs the radio: build a tiny signed .app under the user's
cache that DOES declare it, and launch the tool inside it through
launchd (`open -a`, which is what hands the bundle a TCC identity of its
own - invoking the executable directly inherits the caller's and aborts
just the same).

kble.py worked this out first; its own docstring has the fuller account,
including why the bundle's executable is a shell script and not a copy of
the interpreter (a Mach-O python carries its own embedded __info_plist,
and an embedded one wins over the bundle's - the key would be ignored and
the abort would look identical). This module is that machinery lifted out
so a second tool does not reimplement it, and BOTH callers use it:
kmirror.py's --ble transport and kble.py itself.

kble.py kept its own copy for a while, on the grounds that it was already
committed and bench-verified. That is what every duplicated mechanism says
about itself, and it is not a reason - the pair drifts the moment one side
is fixed and the other is not. One copy, two callers.

What is duplicated ON PURPOSE, and not by this module: the BLE session
itself - attach, subscribe, the pairing-retry dance, whatever holds the
link and serves it over a socket. kble's is one characteristic and
line-oriented; kmirror's is three characteristics with a binary frame
stream mixed in. Collapsing those into one abstraction would trade a real
difference in shape for a smaller diff.

Stdlib only, same reason as kpayload.py: whichever tool's bridge
re-executes inside the bleak venv must be able to import this with
nothing but bleak itself installed alongside the standard library.
"""

import os
import plistlib
import shutil
import subprocess
import sys


def python_with_bleak(cache_dir):
    """A Python that can import bleak, building a venv under cache_dir once.

    Done on the CALLING side, never inside the bundle: the bundle is
    launched detached by launchd, so a first-run `pip install` there would
    be several silent minutes ending in a timeout nobody could explain.
    `cache_dir` is per-tool (kble.py keeps its own, kmirror.py has its own)
    rather than one shared venv, so a first run of either never risks the
    other's already-working copy.
    """
    try:
        import bleak  # noqa: F401
        return sys.executable
    except ImportError:
        pass
    venv = os.path.join(cache_dir, "venv")
    py = os.path.join(venv, "bin", "python3")
    if not os.path.exists(py):
        print("kblebundle: bleak is missing; building the helper venv "
              "(once, ~20s)", file=sys.stderr)
        os.makedirs(cache_dir, exist_ok=True)
        subprocess.check_call([sys.executable, "-m", "venv", venv])
        subprocess.check_call([py, "-m", "pip", "-q", "install", "bleak"])
    return py


def ensure_bundle(app_dir, exe_name, bundle_id, display_name, usage_text,
                  py, script_src, script_copy, log_path):
    """Build (or refresh) a signed .app that runs a COPY of `script_src`
    under `py`, with its stdout/stderr sent to `log_path` (launchd owns the
    bundle's real stdout, so a bridge that dies silently is otherwise the
    one failure this whole arrangement makes hard to diagnose).

    The script is copied rather than run from its working-tree path for
    the same reason kble.py copies itself: the bundle's TCC identity has
    no access to wherever the checkout happens to live, only to its own
    cache directory.

    @param app_dir      Where to build the bundle, e.g. .../KodeMirror.app.
    @param exe_name     CFBundleExecutable - must match the file this
                        writes under Contents/MacOS.
    @param bundle_id    CFBundleIdentifier; also the codesign identifier.
    @param script_src   The script to copy in and run (its own __file__).
    @param script_copy  Where to copy it to inside the cache.
    """
    os.makedirs(os.path.join(app_dir, "Contents", "MacOS"), exist_ok=True)
    shutil.copyfile(script_src, script_copy)
    info = {
        "CFBundleName": display_name,
        "CFBundleDisplayName": display_name,
        "CFBundleIdentifier": bundle_id,
        "CFBundleExecutable": exe_name,
        "CFBundlePackageType": "APPL",
        "CFBundleVersion": "1",
        "NSBluetoothAlwaysUsageDescription": usage_text,
        "NSBluetoothPeripheralUsageDescription": usage_text,
    }
    with open(os.path.join(app_dir, "Contents", "Info.plist"), "wb") as f:
        plistlib.dump(info, f)
    exe = os.path.join(app_dir, "Contents", "MacOS", exe_name)
    with open(exe, "w") as f:
        f.write("#!/bin/sh\n"
                "# Launched by launchd via `open -a`, which is what gives\n"
                "# this bundle a TCC identity of its own. Anything else\n"
                "# inherits the caller's and is refused.\n"
                "#\n"
                "# Its stdout belongs to launchd, so it goes to a file: a\n"
                "# bridge that dies silently is the one failure this whole\n"
                "# arrangement makes hard to diagnose.\n"
                f'exec >>"{log_path}" 2>&1\n'
                'echo "--- $(date) ---"\n'
                f'exec "{py}" "{script_copy}" "$@"\n')
    os.chmod(exe, 0o755)
    subprocess.check_call(["codesign", "--force", "--sign", "-",
                           "--identifier", bundle_id, app_dir],
                          stdout=subprocess.DEVNULL,
                          stderr=subprocess.DEVNULL)


def launch(app_dir, extra_args):
    """`open -a` a bundle built by ensure_bundle(), detached from this
    process.

    `open -a` will not start a second copy of a bundle already running -
    it brings the first one forward and drops the arguments. Fine here,
    and even wanted: the caller only reaches this once nothing answers on
    its socket, and a bundle that is alive but not listening is stuck,
    which the caller's own timeout is what reports it.
    """
    subprocess.check_call(["open", "-a", app_dir, "--args", *extra_args])
