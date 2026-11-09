# tools/factory

The program of the final test station (E5). It runs on the factory computer,
drives one Kode Dot through its last test over the USB console, decides PASS
or FAIL, and writes the report. Read this page when you set up a station, run
the test, or change the program.

The Kode does the measuring; the computer decides. The program speaks the
console's `test_dot_*` verbs, which only a kodeOS built with the station's
service answers: a Kode without them is refused before anything is written.
A PASS runs the Kode's factory reset and then locks the station verbs on that
Kode, before the verdict is published: a lock the Kode does not confirm turns
the run into a FAIL of its own (`lock: failed`). A FAIL leaves the Kode's
verdict screen up and erases nothing.

## The files

| File | What it does |
|---|---|
| `test_dot_final.py` | The program. One run per Kode: wait for the USB, check the Kode, place it under the camera, run every row, judge, write the report, save the photos, lock or leave. |
| `test_dot_station.json` | The template of the station's settings: its name, the two Wi-Fi networks and their channels, the BLE beacon, the RSSI thresholds, the placement limits. |
| `test_dot_ble.py` | The BLE beacon the Kode must hear, advertised from the computer (macOS). |
| `test_dot_pads.py` | Tests of the rear pads block against a fake station, no board. |
| `test_dot_parallel.py` | Tests of what the computer does while the operator presses and touches, no board. |
| `ui/test_dot_ui.py` | The operator's page: a local web server that starts the program and shows the run as it happens. |
| `ui/page.html` | The page itself. |
| `ui/EVENTS.md` | The events the program prints for the page, one per line. |
| `ui/reports.py` | The simple reports: one page per run with what fails in plain words and the pictures, and the list of runs. |
| `ui/test_dot_sim.py` | A fake Kode that answers like a real one on the bench, for the page, the tests and a station with no Kode at hand. |

## Setting up a station

1. Install Python 3 with pyserial and Pillow (`pip install pyserial pillow`).
   The beacon needs CoreBluetooth's pyobjc binding on macOS
   (`pip install pyobjc-framework-CoreBluetooth`).
2. Copy `test_dot_station.json` to `~/kode-factory/test_dot_station.json` (the
   program does it on its first run) and set `station`, `ssid_24` and `ssid_5`
   to the networks of the place.
3. Fill the two channels: with a Kode on the cable, run
   `python3 tools/factory/test_dot_final.py --discover`. It lists the networks
   the Kode hears and writes the channels of the two named ones.

## Running the test

```
python3 tools/factory/ui/test_dot_ui.py -p /dev/cu.usbmodem101
```

This opens the page at `http://localhost:8770`. The operator presses Start,
plugs in the Kode and follows the page. The program alone, without the page:

```
python3 tools/factory/test_dot_final.py [--port P] [--events] [--no-place] [--no-write]
                                        [--operator-sim] [--discover] [--out DIR]
                                        [--config FILE] [--no-open]
```

- `--events` prints one `@@TD {json}` line per event (`ui/EVENTS.md`).
- `--no-place` skips the placement under the camera.
- `--no-write` reads the NFC tag without writing it.
- `--operator-sim` presses the buttons and touches the glass through the
  console's injection verbs: nobody at the station.

These three make a bench run: its verdict is a `BENCH` one, never recorded in
the CSV, and the Kode is never reset nor locked. A production run uses none of
them.
- `--discover` fills the station's Wi-Fi channels and exits.

NFC is run and reported like every other step but does not decide the
verdict: a Kode whose only failure is NFC passes (`OPTIONAL` in
`test_dot_final.py`).

Exit status: 0 for a PASS, 1 for any other verdict, 2 when no report was
written (the port held by another program, a Kode that does not answer, a
kodeOS without the station verbs), 3 when everything passed but the Kode did
not confirm its lock: that Kode must not leave the station.

## Reports

Every run writes a folder `<date>-<time>_<serial>` under `--out`
(`~/kode-factory/reports` by default; never inside this repository) with
`report.html`, `results.json`, the camera's pictures and the photos of the
run. `record_<station>.csv` in the same directory gets one line per Kode
whose exit record passed, with its verdict and its lock (`ok`, `failed`,
`not_sent`). A run of the same serial within 30 minutes of a run
that did not pass is a repeat: both reports stay and the latest counts.

`python3 tools/factory/ui/reports.py` writes the simple reports for every
folder; the page serves them at `/reports/`.

## Without a Kode

```
python3 tools/factory/ui/test_dot_ui.py --simulator [-- --case fail]
```

runs the page against the fake Kode, with the reports in
`~/kode-factory/sim-reports`. The cases are `pass`, `fail`, `nfc`, `buttons`,
`noverdict`, `timeouts`, `misplaced`, `no_camera` and `no_usb`;
`TEST_DOT_SPEED` (or `--speed`) runs the fake faster than a real Kode.
`python3 tools/factory/ui/test_dot_sim.py --case pass` runs the program alone
against it.

## Tests

`tools/tool_tests.sh` runs `tools/tests/test_factory.py` (the whole program
against the simulator, in virtual time), `test_dot_pads.py` and
`test_dot_parallel.py`. None needs a board.
