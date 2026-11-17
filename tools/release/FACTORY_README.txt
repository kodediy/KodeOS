kodeOS factory pack
====================

Six files to flash, one esptool call - an erase-and-write over the four
offsets `idf.py flash` itself uses, the apps region with the welcome app
installed (apps.bin), and the internal FAT, which carries Kody's pack
(kode/) and the C5 image of this release (c5_slave.bin):

  0x2000   bootloader.bin
  0x8000   partition-table.bin
  0x1B000  ota_data_initial.bin
  0x20000  kodeos.bin
  0xC40000 apps.bin
  0x1800000 storage.bin

You need python3 with esptool 5 or newer on it:

    python3 -m pip install 'esptool>=5'

esptool 5 renamed its commands to the dashed form used below (`write-flash`);
an older esptool refuses it. flash.sh checks the version before it touches
the board. A clean macOS ships python3 and no `python`; an ESP-IDF shell has
both, and either works.

Easiest path - run the included script:

    ./flash.sh /dev/tty.usbmodemXXXX

Or by hand:

    python3 -m esptool --chip esp32p4 -p /dev/tty.usbmodemXXXX write-flash \
      --erase-all \
      0x2000   bootloader.bin \
      0x8000   partition-table.bin \
      0x1B000  ota_data_initial.bin \
      0x20000  kodeos.bin \
      0xC40000 apps.bin \
      0x1800000 storage.bin

What this does: erases the whole chip, then puts the board's boot state
back to a brand-new board's - blank otadata, kodeOS (factory) installed,
the welcome app installed and not asked to run, and Kody's pack on the
internal FAT. kodeOS boots; the factory reset that the final station runs
on a PASS asks for the welcome app at the first power-on after ship mode.
The whole erase is on purpose: a board may come with an older partition
table, or with a person's data anywhere on the chip, and only a whole erase
promises that none of it survives.

What survives: nothing - the erase touches the whole chip, not just the six
things this flash writes. Settings, remembered networks, BLE bonds, the
touch calibration, the apps installed before (only the welcome app is
installed after), the mailbox, and internal storage (the FAT filesystem)
all go. Copy anything you want to keep off the board before
flashing (`file_get name=...` on the console); this flash does not ask
again.

The production line's write, for a bare Main board on the jig:

    ./flash.sh /dev/tty.usbmodemXXXX --line

writes the first four files only and erases nothing whole. Instead it
writes 0xFF over the five places a previous pass can leave something that
acts at boot: nvs (0xB000, 64 KB), the core dump (0xC20000, 128 KB), the
apps region's two directories and boot request (0xC40000, 12 KB), and the
mailbox's first sector and its intent (0x1A00000 and 0x1FFF000, 4 KB
each). apps.bin and storage.bin are not written: the assembled Kode gets
the apps region and the FAT from the station's SD later on the line. A
board flashed by hand, away from the line, takes the plain command above.

Verify afterward over the console (USB-C, native USB-Serial/JTAG):

    boot_info

answers one record and then `>ok`:

    >|running=factory addr=0x020000 size_kb=4096 otadata=blank reason=cold

`running=factory` and `otadata=blank` are the two words that matter;
`size_kb` is the slot's size and `reason` is why the board last booted.

THIRD_PARTY_NOTICES.txt lists the third-party software compiled into
kodeos.bin and the licences it is used under. It is not flashed.

Full detail: "Building and flashing" in the kodeOS docs,
https://docs.kode.diy/internals/building-and-flashing
