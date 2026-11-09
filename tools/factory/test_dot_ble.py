#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""The station's Bluetooth part: the Mac advertises a beacon, the Kode listens.

The Mac only advertises: one BLE advert carrying a local name of its own
(CBPeripheralManager, through pyobjc), and the Kode recognises it by that name
- a Mac's address rotates, so it cannot be the key. Whether the Mac hears the
Kode is not tested: BLE lives in the Espressif chip, already validated, and
receiving is enough.

macOS only. The package, for the system Python:
    pip install --user pyobjc-framework-CoreBluetooth

From test_dot_final.py:
    beacon = Beacon("KE5-BEACON"); beacon.on()     # before test_dot_radio
    ... test_dot_radio ...
    beacon.off()

Command line:  python3 test_dot_ble.py [--seconds 5] [--name KE5-BEACON]

If macOS asks for Bluetooth permission for the terminal, it is granted by
hand; this module never tries to get round it.
"""

import argparse
import threading
import time

DEFAULT_NAME = "KE5-BEACON"
START_WAIT_S = 3.0          # how long on() waits for the adapter and the first advert

_DELEGATE = None            # the Objective-C class, defined once per process


def authorisation():
    """'allowed', 'denied', 'undecided', 'restricted' or 'unknown'
    (CBManager.authorization)."""
    try:
        import CoreBluetooth as CB
        a = int(CB.CBManager.authorization())
        return {3: "allowed", 2: "denied", 0: "undecided", 1: "restricted"}.get(a, "unknown")
    except Exception:
        return "unknown"


class Advert:
    """The Mac's BLE advert with a local name. start() waits for the adapter
    to be powered on."""

    def __init__(self, name):
        self.name, self.state, self.error, self.advertising = name, None, None, False
        self._mgr = self._delegate = self._queue = None
        self._ready = threading.Event()

    def start(self, wait=START_WAIT_S):
        import CoreBluetooth as CB
        import libdispatch as D
        from Foundation import NSObject
        global _DELEGATE
        if _DELEGATE is None:          # redefining an Objective-C class in one process is an error
            class Delegate(NSObject):
                def peripheralManagerDidUpdateState_(self, m):
                    outer = self.outer
                    outer.state = int(m.state())
                    if outer.state == CB.CBManagerStatePoweredOn:
                        m.startAdvertising_({CB.CBAdvertisementDataLocalNameKey: outer.name})
                    else:
                        outer._ready.set()

                def peripheralManagerDidStartAdvertising_error_(self, m, err):
                    outer = self.outer
                    outer.error = None if err is None else str(err)
                    outer.advertising = err is None
                    outer._ready.set()
            _DELEGATE = Delegate

        self._queue = D.dispatch_queue_create(b"kode.test_dot.beacon", None)
        self._delegate = _DELEGATE.alloc().init()
        self._delegate.outer = self
        self._mgr = CB.CBPeripheralManager.alloc().initWithDelegate_queue_(self._delegate, self._queue)
        self._ready.wait(wait)
        return self.advertising

    def stop(self):
        try:
            if self._mgr is not None:
                self._mgr.stopAdvertising()
        except Exception:
            pass
        self.advertising = False
        self._mgr = self._delegate = self._queue = None


class Beacon:
    """The beacon alone: the Kode is the one that listens."""

    def __init__(self, name=DEFAULT_NAME):
        self.advert = Advert(name)
        self.name = name

    def on(self):
        """Start advertising. Never raises: a Mac without the package or
        without permission still lets the Kode try (it may hear another
        device), and the reason goes into the report."""
        try:
            sent = self.advert.start()
        except Exception as e:
            return dict(beacon=False, error=f"{type(e).__name__}: {e}", authorisation=authorisation())
        return dict(beacon=sent, error=self.advert.error, bt_state=self.advert.state,
                    authorisation=authorisation())

    def off(self):
        self.advert.stop()


if __name__ == "__main__":
    ap = argparse.ArgumentParser(description="Advertise the station's BLE beacon for a few seconds (no Kode needed)")
    ap.add_argument("--seconds", type=float, default=5.0)
    ap.add_argument("--name", default=DEFAULT_NAME)
    a = ap.parse_args()
    print("Bluetooth authorisation:", authorisation(), flush=True)
    b = Beacon(a.name)
    print("beacon:", b.on(), flush=True)
    time.sleep(a.seconds)
    b.off()
