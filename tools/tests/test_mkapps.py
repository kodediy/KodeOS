#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""mkapps against the board's own reader: kapps_fmt.h judges what it writes.

A tool that writes a format and a test that reads it back with the same tool
would agree with each other and with nothing on the board. So the region
image mkapps builds is handed to a small C program compiled against
kapps_fmt.h itself, which runs the checks a boot runs - kapps_dir_classify()
on both copies, kapps_dir_pick(), kapps_req_live() on the request - with the
ROM's CRC written out in C, not taken from the Python side.

    python3 tools/tests/test_mkapps.py
"""

import os
import shutil
import struct
import subprocess
import sys
import tempfile
import unittest

TOOLS = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ROOT = os.path.dirname(TOOLS)
sys.path.insert(0, TOOLS)
import mkapps  # noqa: E402

FMT_H = os.path.join(ROOT, mkapps.FMT_H)
PARTITIONS_CSV = os.path.join(ROOT, mkapps.PARTITIONS_CSV)

CHECKER = r"""
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "kapps_fmt.h"

/* The ROM's esp_rom_crc32_le(), as kdrv_apps calls it with UINT32_MAX. */
static uint32_t rom_crc32_le(uint32_t crc, const uint8_t *p, size_t n)
{
    crc = ~crc;
    for (size_t i = 0U; i < n; ++i) {
        crc ^= p[i];
        for (int b = 0; b < 8; ++b) {
            crc = (crc >> 1) ^ (0xEDB88320U & (0U - (crc & 1U)));
        }
    }
    return ~crc;
}

static uint8_t region[KAPPS_DATA_OFF];

int main(int argc, char **argv)
{
    FILE *f = fopen(argv[1], "rb");
    unsigned long region_size = strtoul(argv[2], NULL, 0);
    kapps_dir_t a, b;
    kapps_req_t req;
    if ((f == NULL) || (fread(region, 1U, sizeof(region), f) != sizeof(region))) {
        return 2;
    }
    memcpy(&a, &region[KAPPS_DIR_A_OFF], sizeof(a));
    memcpy(&b, &region[KAPPS_DIR_B_OFF], sizeof(b));
    memcpy(&req, &region[KAPPS_REQ_OFF], sizeof(req));
    kapps_dir_copy_t ka = kapps_dir_classify(&a, rom_crc32_le(UINT32_MAX,
                          (const uint8_t *)&a, offsetof(kapps_dir_t, crc)), region_size);
    kapps_dir_copy_t kb = kapps_dir_classify(&b, rom_crc32_le(UINT32_MAX,
                          (const uint8_t *)&b, offsetof(kapps_dir_t, crc)), region_size);
    bool live = kapps_req_live(&req, rom_crc32_le(UINT32_MAX,
                               (const uint8_t *)&req, offsetof(kapps_req_t, crc)));
    printf("a=%d b=%d pick=%d count=%u gen=%u off=0x%x size=%u seq=%u flags=%u req_live=%d\n",
           (int)ka, (int)kb,
           (int)kapps_dir_pick(ka == KAPPS_DIR_COPY_OK, a.gen, kb == KAPPS_DIR_COPY_OK, b.gen),
           (unsigned)a.count, (unsigned)a.gen, (unsigned)a.e[0].offset,
           (unsigned)a.e[0].size, (unsigned)a.e[0].seq, (unsigned)a.e[0].flags, (int)live);
    return 0;
}
"""


def fake_image(project: str, length: int = 0x3000) -> bytes:
    """An ESP image just real enough to carry an app descriptor."""
    img = bytearray(length)
    img[0] = 0xE9
    struct.pack_into("<I", img, mkapps.APP_DESC_OFF, mkapps.APP_DESC_MAGIC)
    name = project.encode("ascii")
    img[mkapps.APP_DESC_PROJECT_OFF:mkapps.APP_DESC_PROJECT_OFF + len(name)] = name
    img[-1] = 0x5A
    return bytes(img)


class HeaderTests(unittest.TestCase):
    def test_reads_every_number_it_needs(self):
        fmt = mkapps.read_fmt(FMT_H)
        self.assertEqual(set(fmt), set(mkapps.FMT_NAMES))
        self.assertEqual(fmt["KAPPS_ENTRY_VALID"], 1)
        self.assertEqual(fmt["KAPPS_DATA_OFF"] % fmt["KAPPS_ALIGN"], 0)

    def test_finds_the_region(self):
        offset, size = mkapps.read_region(PARTITIONS_CSV)
        self.assertGreater(offset, 0)
        self.assertGreater(size, 0)


class BoardReadsItTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cc = shutil.which("cc")
        if cc is None:
            raise RuntimeError("no C compiler: this test needs one to judge mkapps with kapps_fmt.h")
        cls.tmp = tempfile.mkdtemp()
        src = os.path.join(cls.tmp, "check.c")
        cls.checker = os.path.join(cls.tmp, "check")
        with open(src, "w", encoding="ascii") as fh:
            fh.write(CHECKER)
        subprocess.run([cc, "-std=c11", "-Wall", "-Werror", "-I", os.path.dirname(FMT_H),
                        "-o", cls.checker, src], check=True)

    @classmethod
    def tearDownClass(cls):
        shutil.rmtree(cls.tmp)

    def judge(self, blob: bytes, region_size: int) -> dict:
        path = os.path.join(self.tmp, "apps.bin")
        with open(path, "wb") as fh:
            fh.write(blob)
        out = subprocess.run([self.checker, path, str(region_size)], check=True,
                             capture_output=True, text=True).stdout.split()
        return {k: v for k, v in (w.split("=") for w in out)}

    def test_one_installed_app_and_no_request(self):
        fmt = mkapps.read_fmt(FMT_H)
        _, size = mkapps.read_region(PARTITIONS_CSV)
        image = fake_image("welcome")
        blob = mkapps.build(image, fmt, size)
        got = self.judge(blob, size)
        self.assertEqual(got["a"], "0", "copy A must be KAPPS_DIR_COPY_OK")
        self.assertEqual(got["b"], "1", "copy B must be KAPPS_DIR_COPY_BLANK")
        self.assertEqual(got["pick"], "0", "a boot must pick copy A")
        self.assertEqual(got["count"], "1")
        self.assertEqual(got["gen"], "1")
        self.assertEqual(int(got["off"], 0), fmt["KAPPS_DATA_OFF"])
        self.assertEqual(int(got["size"]), len(image))
        self.assertEqual(got["seq"], "1")
        self.assertEqual(got["flags"], "1")
        self.assertEqual(got["req_live"], "0", "the pack must carry no boot request")
        data = fmt["KAPPS_DATA_OFF"]
        self.assertEqual(blob[data:data + len(image)], image)
        self.assertEqual(len(blob) % mkapps.WRITE_ALIGN, 0)


class RefusalTests(unittest.TestCase):
    def run_main(self, image: bytes, project: str) -> int:
        with tempfile.TemporaryDirectory() as tmp:
            src = os.path.join(tmp, "app.bin")
            with open(src, "wb") as fh:
                fh.write(image)
            argv = sys.argv
            sys.argv = ["mkapps.py", "--project", project, "--fmt", FMT_H,
                        "--partitions", PARTITIONS_CSV, src, os.path.join(tmp, "apps.bin")]
            try:
                return mkapps.main()
            finally:
                sys.argv = argv

    def test_accepts_the_named_project(self):
        self.assertEqual(self.run_main(fake_image("welcome"), "welcome"), 0)

    def test_refuses_another_project(self):
        self.assertEqual(self.run_main(fake_image("kodeos"), "welcome"), 1)

    def test_refuses_what_is_not_an_esp_image(self):
        self.assertEqual(self.run_main(b"\x00" * 0x100, "welcome"), 1)

    def test_refuses_an_image_with_no_descriptor(self):
        img = bytearray(fake_image("welcome"))
        img[mkapps.APP_DESC_OFF] ^= 0xFF
        self.assertEqual(self.run_main(bytes(img), "welcome"), 1)

    def test_refuses_an_image_the_region_cannot_hold(self):
        fmt = mkapps.read_fmt(FMT_H)
        _, size = mkapps.read_region(PARTITIONS_CSV)
        with self.assertRaises(ValueError):
            mkapps.build(fake_image("welcome", size - fmt["KAPPS_DATA_OFF"] + 1), fmt, size)


if __name__ == "__main__":
    unittest.main()
