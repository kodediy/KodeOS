# apps_image

This folder holds the welcome app, the one app a Kode Dot leaves the factory with: `kodediy/kode-app-onboarding`, a private repository of Kode, the folder `apps/bienvenida`, project `onboarding`. `sdkconfig.defaults` names it in `CONFIG_KODEOS_WELCOME_APP`.

`welcome.pin` is the one file here in git: one line, `<repository> <app folder> <commit>`. `tools/build_welcome.sh` clones that repository into `build_welcome/src`, builds the folder at that commit and writes two files here, `<project>.bin` and its stamp `<project>.commit` ("<commit> <sha256>", `-dirty` on local changes). Moving the pin is a deliberate change, like moving the C5's.

`tools/assemble_release.sh` builds the factory pack's `apps.bin` from the image with `tools/mkapps.py`: the apps region's directory with the app installed in it, the image at the first page of data, and no boot request. `flash.sh` writes it at the region's base. kodeOS boots on it and lists the app in Apps. The factory reset that the final station runs on a PASS keeps the app and writes its boot request right before ship mode, so the customer's first power-on runs it.

The release refuses an image that is missing, has no stamp, was built from another commit than the pin, is not the stamped one, or carries another project name than `CONFIG_KODEOS_WELCOME_APP`.

**The image and its stamp are not in git.** `.gitignore` excludes `*.bin` and `*.commit` in this folder. A release runs `tools/build_welcome.sh`, which clones the pinned commit over SSH, so it needs a key that reads kode-app-onboarding: only Kode's does. A fork points `welcome.pin` at a welcome app of its own.
