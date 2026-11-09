# storage_image

This folder holds the files that the build writes into the `storage` partition, beside Kody's pack. One of them, `c5_slave.bin`, is on every board: the factory pack carries the firmware of the C5 that its own release was built with. Every other file here is for the bench only, and `tools/assemble_release.sh` refuses to package a release while one is here.

The `storage` partition of the flash belongs to **the user**, with one exception. It is a FAT volume of 2 MB with wear levelling. A user receives a board with this partition holding two things of kodeOS: the folder `kode/` and the file `c5_slave.bin`. `kode/` holds Kody's pack, the animations that kodeOS reads, and an Update in About replaces it. `c5_slave.bin` is the C5 image of this kodeOS, so a C5 can always be put back to the firmware it shipped with (`install src="fs:c5_slave.bin" dst=c5`). Leave both alone. The build writes the pack into the image from `sdk/components/kui/kody/pack/`, beside everything in this folder. This command writes that image to the partition:

```bash
idf.py -p PORT storage-flash
```

`PORT` is the serial port of the board. [Building and flashing](https://docs.kode.diy/internals/building-and-flashing) shows how to find its name.

The command replaces the whole partition. A plain `idf.py flash` never changes the partition. The files in this folder and the pack (about 70 KB) must fit in the volume, which holds about 2,016 KB. `c5_slave.bin` takes about 1,310 KB of it. Wear levelling uses the rest of the 2 MB.

**The image files are not in git.** The file `.gitignore` excludes `*.bin`, `*.kfw` and `*.commit` in this folder, so the folder looks empty on GitHub. Each file is built in another folder of the repository. `tools/build_c5.sh` writes `c5_slave.bin`; a person copies any other file here when a test needs it on a board.

| File | Built by | Build it again when |
|---|---|---|
| `c5_slave.bin` and `c5_slave.commit` | `tools/build_c5.sh`, from the C5 submodule | the commit of the C5 submodule changes |
| `*.kfw` | `tools/mkkfw.py`, from the `.bin` of the same name | its `.bin` is built again |

After `storage-flash`, the console command `fs_ls` lists the files. The console command `install` installs an app from one of them, for example `install src="fs:buttons_led.kfw" dst=app`. The `install` command does not need this folder. Its `src=` also reads `sd:`, `http://`, `https://` and `pending`.

**`kodeos.bin` does not go in this folder.** The build of this repository writes it as `build/kodeos.bin`. That file is about 2,725 KB, and the volume holds about 2,016 KB, so the file does not fit, even alone. A board gets a new kodeOS in one of these ways, and none of them uses the volume:

- `python3 tools/kcli.py -p PORT install build/kodeos.bin --dst os` sends the image over the cable.
- `install src="sd:kodeos.bin" dst=os` reads the file `kodeos.bin` from the microSD card.
- `install src="https://..." dst=os` downloads the image. The dots stand for the address of the file.
- `install src="pending" dst=os` installs the image that a computer wrote into the mailbox.

This repository cannot build `c5_slave.bin` in its normal build, because that firmware is for another chip. `tools/build_c5.sh` builds it with ESP-IDF 6.0.2 and writes `c5_slave.commit` beside it: the commit of `sdk/c5` and the SHA-256 of the image. The build keeps that stamp out of the volume. `tools/assemble_release.sh` checks three things and refuses the release when one fails. The stamp must be the commit that the SDK pins. The image must be the stamped one. The volume must hold only `kode/` and `c5_slave.bin`.

See [Apps and examples](https://docs.kode.diy/internals/apps-and-examples) and [Building and flashing](https://docs.kode.diy/internals/building-and-flashing).
