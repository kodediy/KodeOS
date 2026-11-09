# tools

The scripts that run on a computer: the programs that talk to the board, the release scripts, and the checks. Nothing in this folder is built into a firmware. Read this page when you want to use a script of this folder or change one.

The comment at the top of each script explains why the script exists. The tables below say what each one does. The documentation at docs.kode.diy has the full instructions, and each table links to its page.

## Talking to the board

| Script | What it does |
|---|---|
| `kcli.py` | The console over the USB-C cable, or over the port of the charging base. It sends commands, and it has `install`, `screenshot`, `file-get`, `file-put` and `appdata ls\|info\|rm\|put`. It is the reference client of the protocol: the program that shows how a computer uses the console. |
| `kble.py` | The same console over Bluetooth. A small bridge process keeps the link open between two commands, so the second command is fast. |
| `kpayload.py` | Code that `kcli.py` and `kble.py` share: `quote()` writes a text argument, `fields()` reads a `>\|` record, and the rest decodes the base64 records and builds the PNG of a screenshot. It uses the standard library only. |
| `kappdata.py` | The `appdata put` part of `kcli.py`. It checks `app.json` and converts a PNG or JPEG icon to the `.k64` format of the board. The conversion needs Pillow, a Python library for images (`pip install pillow`). |
| `kmirror.py` | Starts the mirror over the cable (`-p`) or over Bluetooth (`--ble`), and shows the screen in a browser tab. A click is a tap, and a button pad is on the page. |
| `kblebundle.py` | What a script needs to use Bluetooth on macOS: a Python environment with bleak, and a signed `.app` that declares the Bluetooth permission. `kble.py` and `kmirror.py` use it. |

`kcli.py` and the cable mode of `kmirror.py` (`-p`) need pyserial, a Python library for the serial port. ESP-IDF brings its own Python with pyserial, so run them in a terminal where ESP-IDF is loaded.

See [Bench tools](https://docs.kode.diy/internals/bench-tools) and [The console protocol](https://docs.kode.diy/internals/console-protocol).

## Building and flashing

| Script | What it does |
|---|---|
| `jtag_flash.sh` | Writes the flash over JTAG. It starts one OpenOCD server, writes the images, and stops the server. |
| `build_c5.sh` | Builds the firmware of the ESP32-C5 from the submodule into `storage_image/c5_slave.bin`, the image every factory pack carries, and stamps its commit in `c5_slave.commit`. |

See [Building and flashing](https://docs.kode.diy/internals/building-and-flashing).

## Checks

This part is for a person who changes the code of kodeOS. To use the board, you do not need it.

| Script | What it does |
|---|---|
| `check_layers.sh` | Checks the include rules of the layers. It checks the LVGL rule here, and then runs the script of the SDK for the other rules. It must end with status 0 before a change is done. |
| `host_tests.sh` | Runs the unit tests of the SDK on the computer (Unity and CMock on the linux target of ESP-IDF). |
| `tool_tests.sh` | Runs every test in `tools/tests/`, and the station's two tests in `tools/factory/`, with no board and no ESP-IDF. Only `test_kappdata.py`, `test_mkkfw.sh` and `test_factory.py` need Pillow. It fails when a `tools/tests/test_*` file exists that it does not run. |
| `check_os_size.sh` | Checks that `build/kodeos.bin` is small enough for an update through the mailbox. |
| `ram_budget.txt` | The budget of internal RAM for kodeOS. It gives the highest allowed size of the static image. It also lists the static variables of 2048 bytes or more that are in internal RAM. A static variable keeps one fixed place in memory while the program runs. `sdk/tools/check_ram.sh build/kodeos.elf tools/ram_budget.txt` checks a build against it. |
| `misra_baseline.txt` | The MISRA C baseline of kodeOS: the version of cppcheck it was taken with, and, for each file and each id, the number of findings that the file must not exceed. `sdk/tools/check_misra.sh build/compile_commands.json tools/misra_baseline.txt sdk/components components/kgui` checks a build against it. The same command with `--update` first writes the file again. |
| `check_todo_refs.sh` | Fails when a change adds a `TODO`, `FIXME`, `HACK` or `XXX` that names no issue, for example `TODO(#42)`. It reads only the lines that the change adds against a base commit. CI runs it on every pull request. |
| `ci_report.sh` | Turns the output of the size, RAM and MISRA checks into one Markdown table. CI writes the table to the summary of the run and to one comment on the pull request. |

See [Host tests](https://docs.kode.diy/internals/host-tests) and [Design rules](https://docs.kode.diy/sdk/design-rules).

## The factory station

| Folder | What it does |
|---|---|
| `factory/` | The program of the final test station. It runs on the factory computer, drives one Kode Dot through its last test over the USB console, decides PASS or FAIL, and writes the report. `factory/ui/` is the page the operator looks at, and a simulator that runs the whole test against a fake Kode. It needs pyserial and Pillow. |

See [`factory/README.md`](factory/README.md).

## Releases

| Script | What it does |
|---|---|
| `validate_release_tag.sh` | Accepts or rejects a tag, and gives the version text for the build. The format of a tag is checked in this one place. |
| `assemble_release.sh` | Makes the release files from a finished build: the factory pack and the single image. |
| `check_on_main.sh` | Fails when a commit is not on a branch. The release refuses a tag that is not on `main`, and a C5 commit that is not on the C5's `main`. |
| `mkkfw.py` | Puts an ESP-IDF image in a Kode firmware container: a 72-byte header, which is the first part of the container, and a compressed payload. With `--appdata DIR` it also packs the app's `app.json` and icon. |
| `release/` | The three files `assemble_release.sh` copies into the factory pack: `flash.sh`, `FACTORY_README.txt` and `THIRD_PARTY_NOTICES.txt`. |

See [Release pipeline](https://docs.kode.diy/internals/release-pipeline).

## Tests for the tools

The tests live in `tools/tests/`. `test_kble.py`, `test_kmirror.py`, `test_kappdata.py`, `test_kpayload.py`, `test_mkkfw.sh`, `test_assemble_release.sh` and `test_validate_release_tag.sh` test the scripts above, and `test_factory.py` tests the station program against its simulator. The station keeps two more tests next to its program, `factory/test_dot_pads.py` and `factory/test_dot_parallel.py`. They need no board. `tools/tool_tests.sh` runs all of them, and CI runs that script.

This part is for a person who changes the mirror or `mkkfw.py`. `kmirror.py` reads the mirror over two connections, the cable and Bluetooth. It has a separate reader and a separate decoder for each connection. `test_kmirror.py` checks that the two decoders give the same result, and it checks the golden header below.

**The golden header.** The header of a mirror frame is its first bytes. The golden header is a fixed example of it, written by hand. Two programs implement the frame format of the mirror: `kmirror_proto.c` writes it and `kmirror.py` reads it. The tests of each program check it against itself only. So both test sets can pass while the two programs disagree. To find such a difference, the same fourteen bytes are in two files, written by hand: `test_kmirror.py` and `sdk/tests/host/cases/test_kmirror_proto.c`. The copy is on purpose. A header generated by one program would repeat the errors of that program. `test_mkkfw.sh` does the same for the header of `mkkfw.py`.
