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
This release crosses a table change (nvs moves over the old otadata and
phy_init sectors, the apps region's base lands on old image bytes), so a
targeted write cannot clean up after every board's previous table the way a
full erase can. Once every board has been through this release, the erase
comes back out of a future one.

What survives: nothing - the erase touches the whole chip, not just the six
things this flash writes. Settings, remembered networks, BLE bonds, the
touch calibration, the apps installed before (only the welcome app is
installed after), the mailbox, and internal storage (the FAT filesystem)
all go. Copy anything you want to keep off the board before
flashing (`file_get name=...` on the console); this flash does not ask
again.

Verify afterward over the console (USB-C, native USB-Serial/JTAG):

    boot_info

answers one record and then `>ok`:

    >|running=factory addr=0x020000 size_kb=4096 otadata=blank reason=cold

`running=factory` and `otadata=blank` are the two words that matter;
`size_kb` is the slot's size and `reason` is why the board last booted.

THIRD_PARTY_NOTICES.txt lists the third-party software compiled into
kodeos.bin and the licences it is used under. It is not flashed.

Full detail: "Recovery flash: bootloader + factory" in the kodeOS README,
https://github.com/kodediy/KodeOs-demo#readme
