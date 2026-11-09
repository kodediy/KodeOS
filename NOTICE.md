# Licences of kodeOS

This file says which licence applies to each part of this repository. Read it before you distribute kodeOS, or a firmware that you built from it.

| Part | Owner | Licence |
|---|---|---|
| everything that this file does not name below | Kode | GPL-3.0-or-later, with the additional permission below |
| `sdk/` | Kode and others | the file `NOTICE.md` of the SDK says it |
| `components/kgui/fonts/*.c` | the DM Sans and Outfit families, converted to C | SIL Open Font License 1.1; the texts are `sdk/components/kui/fonts/assets/OFL-DMSans.txt` and `OFL-Outfit.txt` |

## The code of Kode

Copyright (C) 2026 Kode.

This program is free software: you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, either version 3 of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.

The file `LICENSE` holds the full text of the GNU General Public License, version 3.

### Additional permission under GNU GPL version 3 section 7

If you modify this Program, or any covered work, by linking or combining it with the RF Abstraction Layer (RFAL) and the ST25R3916 driver of STMicroelectronics (or a modified version of those libraries), containing parts covered by the terms of the STMicroelectronics SLA0051 licence, the licensors of this Program grant you additional permission to convey the resulting work. Corresponding Source for a non-source form of such a combination shall include the source code for the parts of RFAL and of the ST25R3916 driver used as well as that of the covered work.

This permission applies only to the code of which Kode holds the copyright. Kode cannot give it for the code of another owner, and so it does not apply to QP/C.

If you change the code of Kode, you can remove this permission from your version. Section 7 of the GNU General Public License allows this.

## The bootloader

`bootloader_components/main/bootloader_start.c` is code of Kode. A part of it follows the bootloader of ESP-IDF, which is code of Espressif Systems under the Apache License, version 2.0.

## The SDK

The folder `sdk/` is a different repository. It holds the code of Kode, QP/C of Quantum Leaps and RFAL of STMicroelectronics. RFAL is not under the GNU General Public License. The file `NOTICE.md` in that folder says which licence applies to each part.

## The components that the build downloads

The ESP-IDF component manager downloads other components when you build: for example LVGL, esp-hosted and the driver of the display. They are not in this repository. Each one has its own licence, in its folder under `managed_components/`.
