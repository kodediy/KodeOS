# storage_image

This folder holds the files that the build writes into the `storage` partition, beside Kody's pack. One thing here is on every board: `.kode/appsdata/<welcome app>/`, the data folder of the app the factory installs. Every other file here is for the bench only, and `tools/assemble_release.sh` refuses to package a release while one is here.

The `storage` partition of the flash belongs to **the user**, with one exception. It is a FAT volume of 2 MB with wear levelling. A user receives a board with this partition holding two things of kodeOS: the folder `kode/` and the folder `.kode/`. `kode/` holds Kody's pack, the animations that kodeOS reads, and an Update in About replaces it. `.kode/appsdata/` holds one folder for each app that has app data, its `app.json` and `icon.k64`: Apps reads the app's icon, colour and description from it. An app's data travels apart from its image and an install writes no folder, so the image carries the welcome app's folder already. Leave both alone. The build writes the pack into the image from `sdk/components/kui/kody/pack/`, beside everything in this folder. This command writes that image to the partition:

```bash
idf.py -p PORT storage-flash
```

`PORT` is the serial port of the board. [Building and flashing](https://docs.kode.diy/internals/building-and-flashing) shows how to find its name.

The command replaces the whole partition. A plain `idf.py flash` never changes the partition. The files in this folder and the pack (about 70 KB) must fit in the volume, which holds about 2,016 KB. Wear levelling uses the rest of the 2 MB.

**The image files are not in git.** The file `.gitignore` excludes `*.bin` and `.kode/` in this folder, so the folder looks empty on GitHub. `tools/build_welcome.sh` writes `.kode/` from the welcome app's `appdata/` folder; build it again when `apps_image/welcome.pin` moves. A person copies any other file here when a test needs it on a board.

After `storage-flash`, the console command `fs_ls` lists the files. The console command `install` installs an app from one of them, for example `install src="fs:buttons_led.bin" dst=app`. The `install` command does not need this folder. Its `src=` also reads `sd:`, `http://`, `https://` and `pending`.

**`kodeos.bin` does not go in this folder.** The build of this repository writes it as `build/kodeos.bin`. That file is about 2,725 KB, and the volume holds about 2,016 KB, so the file does not fit, even alone. A board gets a new kodeOS in one of these ways, and none of them uses the volume:

- `python3 tools/kcli.py -p PORT install build/kodeos.bin --dst os` sends the image over the cable.
- `install src="sd:kodeos.bin" dst=os` reads the file `kodeos.bin` from the microSD card.
- `install src="https://..." dst=os` downloads the image. The dots stand for the address of the file.
- `install src="pending" dst=os` installs the image that a computer wrote into the mailbox.

**The C5's firmware does not go in this folder either.** A board's C5 is closed at the factory with the signed image of a C5 release, and a closed C5 takes only a signed image; the C5's own repository, `kodediy/kode_slave_c5`, publishes them. Boards made with kodeOS v0.1.1 or earlier carry an unsigned `c5_slave.bin` on this volume; Apps never lists it, and a factory reset leaves it. On the bench, `idf.py -C sdk/c5 -B build_c5 build` builds the open image and `python3 tools/kcli.py -p PORT install build_c5/kode_c5.bin --dst c5` sends it to a C5 that is not closed.

`tools/assemble_release.sh` checks two things here and refuses the release when one fails. The volume must hold only `kode/` and `.kode/`. And `.kode/` must hold only the welcome app's data folder, the one `tools/build_welcome.sh` built and stamped in `apps_image/`.

See [Apps and examples](https://docs.kode.diy/internals/apps-and-examples) and [Building and flashing](https://docs.kode.diy/internals/building-and-flashing).
