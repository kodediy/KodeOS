#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""kappdata's pure half: app.json validation and icon conversion, no board.

`validate` is the same subset the board's own reader parses (the app.json keys
in https://docs.kode.diy/internals/apps-region); this pins the host and the device to one grammar by
testing the host's copy against the spec's own example and its own limits.
`to_k64` is tested against a PNG built on the fly with Pillow, so nothing
under tools/ needs a fixture image checked in - Pillow is the one dependency
that buys, and only this file and kappdata.py itself ever import it.

`load`, `write`, `read` and `digest` are the folder itself: an app's
`appdata/` turned into the two files the board stores, the same bytes whether
the cable, the factory's FAT image or a release carries them.

    python3 tools/tests/test_kappdata.py
"""

import hashlib
import json
import os
import subprocess
import sys
import tempfile
import unittest

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
import kappdata  # noqa: E402

SPEC_EXAMPLE = {
    "kode": "1",
    "name": "Scope",
    "category": "tools",
    "version": "0.4",
    "description": "Two-channel oscilloscope on the expansion header.",
    "icon": "images/scope.k64",
    "color": "#8E8E93",
}


class ValidateTests(unittest.TestCase):
    def test_accepts_the_spec_example(self):
        self.assertEqual(kappdata.validate(SPEC_EXAMPLE), SPEC_EXAMPLE)

    def test_rejects_missing_kode(self):
        obj = dict(SPEC_EXAMPLE)
        del obj["kode"]
        with self.assertRaisesRegex(ValueError, "kode"):
            kappdata.validate(obj)

    def test_rejects_wrong_kode(self):
        obj = dict(SPEC_EXAMPLE, kode="2")
        with self.assertRaisesRegex(ValueError, "kode"):
            kappdata.validate(obj)

    def test_rejects_a_nested_value(self):
        obj = dict(SPEC_EXAMPLE, name={"nested": "x"})
        with self.assertRaisesRegex(ValueError, "name"):
            kappdata.validate(obj)

    def test_rejects_a_name_over_the_limit(self):
        obj = dict(SPEC_EXAMPLE, name="x" * 32)
        with self.assertRaisesRegex(ValueError, "name"):
            kappdata.validate(obj)

    def test_accepts_a_name_at_the_limit(self):
        obj = dict(SPEC_EXAMPLE, name="x" * 31)
        kappdata.validate(obj)

    def test_rejects_a_short_colour(self):
        obj = dict(SPEC_EXAMPLE, color="#FFF")
        with self.assertRaisesRegex(ValueError, "color"):
            kappdata.validate(obj)

    def test_rejects_an_absolute_icon_path(self):
        obj = dict(SPEC_EXAMPLE, icon="/etc/passwd")
        with self.assertRaisesRegex(ValueError, "icon"):
            kappdata.validate(obj)

    def test_rejects_an_icon_path_with_dotdot(self):
        obj = dict(SPEC_EXAMPLE, icon="../icon.k64")
        with self.assertRaisesRegex(ValueError, "icon"):
            kappdata.validate(obj)

    def test_rejects_non_ascii(self):
        obj = dict(SPEC_EXAMPLE, description="café")
        with self.assertRaisesRegex(ValueError, "description"):
            kappdata.validate(obj)

    def test_rejects_a_non_ascii_key(self):
        obj = dict(SPEC_EXAMPLE)
        obj["na\u00efve"] = "x"
        with self.assertRaisesRegex(ValueError, "key is ASCII only"):
            kappdata.validate(obj)

    def test_rejects_a_control_character_in_a_key(self):
        obj = dict(SPEC_EXAMPLE)
        obj["a\tb"] = "x"
        with self.assertRaisesRegex(ValueError, "key is ASCII only"):
            kappdata.validate(obj)

    def test_rejects_not_an_object(self):
        with self.assertRaisesRegex(ValueError, "app.json"):
            kappdata.validate(["kode", "1"])


class FindIconTests(unittest.TestCase):
    def test_named_icon_first(self, ):
        d = self._tmpdir()
        open(os.path.join(d, "icon.png"), "wb").close()
        open(os.path.join(d, "mine.k64"), "wb").close()
        self.assertEqual(kappdata.find_icon(d, {"icon": "mine.k64"}),
                         os.path.join(d, "mine.k64"))

    def test_falls_back_to_icon_png(self):
        d = self._tmpdir()
        open(os.path.join(d, "icon.png"), "wb").close()
        self.assertEqual(kappdata.find_icon(d, {}), os.path.join(d, "icon.png"))

    def test_none_when_nothing_matches(self):
        d = self._tmpdir()
        self.assertIsNone(kappdata.find_icon(d, {}))

    def _tmpdir(self):
        import tempfile
        d = tempfile.mkdtemp()
        self.addCleanup(_rmtree, d)
        return d


def _rmtree(d):
    import shutil
    shutil.rmtree(d, ignore_errors=True)


class ToK64Tests(unittest.TestCase):
    def png(self, size, colour):
        from PIL import Image
        import tempfile

        im = Image.new("RGBA", size, colour)
        with tempfile.NamedTemporaryFile(suffix=".png", delete=False) as f:
            path = f.name
        self.addCleanup(os.remove, path)
        im.save(path)
        return path

    def k64(self, data):
        import tempfile

        with tempfile.NamedTemporaryFile(suffix=".k64", delete=False) as f:
            f.write(data)
            path = f.name
        self.addCleanup(os.remove, path)
        return path

    def test_converts_an_opaque_png_to_the_128_px_block(self):
        data = kappdata.to_k64(self.png((100, 40), (255, 0, 0, 255)))
        self.assertEqual(len(data), 128 * 128 * 2 + 128 * 128)
        alpha = data[128 * 128 * 2:]
        self.assertTrue(all(b == 255 for b in alpha))

    def test_converts_to_the_64_px_block_when_asked(self):
        data = kappdata.to_k64(self.png((100, 40), (255, 0, 0, 255)), side=64)
        self.assertEqual(len(data), 64 * 64 * 2 + 64 * 64)
        alpha = data[64 * 64 * 2:]
        self.assertTrue(all(b == 255 for b in alpha))

    def test_a_pixel_is_rgb565_little_endian_and_alpha_follows_the_colour_plane(self):
        from PIL import Image
        import tempfile

        im = Image.new("RGBA", (256, 256), (255, 0, 0, 255))
        im.paste((0, 0, 255, 255), (128, 0, 256, 128))   # the top right: blue
        im.paste((0, 0, 0, 0), (0, 128, 256, 256))       # the bottom half: clear
        with tempfile.NamedTemporaryFile(suffix=".png", delete=False) as f:
            path = f.name
        self.addCleanup(os.remove, path)
        im.save(path)

        data = kappdata.to_k64(path)
        plane = 128 * 128 * 2
        at = lambda y, x: (y * 128 + x) * 2          # a pixel's place in the colour plane
        self.assertEqual(data[at(10, 10):at(10, 10) + 2], bytes([0x00, 0xF8]))    # 0xF800
        self.assertEqual(data[at(10, 120):at(10, 120) + 2], bytes([0x1F, 0x00]))  # 0x001F
        self.assertEqual(data[plane + 10 * 128 + 10], 255)
        self.assertEqual(data[plane + 10 * 128 + 120], 255)
        self.assertEqual(data[plane + 100 * 128 + 10], 0)

    def test_a_k64_of_either_legal_size_passes_through(self):
        for size in (128 * 128 * 3, 64 * 64 * 3):
            data = bytes(range(256)) * (size // 256)
            self.assertEqual(kappdata.to_k64(self.k64(data)), data)

    def test_a_k64_of_the_wrong_size_is_refused(self):
        path = self.k64(bytes(64 * 64 * 3 - 1))
        with self.assertRaisesRegex(
                ValueError, "icon: .* is 12287 bytes, a .k64 is 12288 or 49152"):
            kappdata.to_k64(path)

    def test_a_128_px_k64_is_refused_when_64_px_is_asked_for(self):
        path = self.k64(bytes(128 * 128 * 3))
        with self.assertRaisesRegex(ValueError, "49152 bytes.*64 px"):
            kappdata.to_k64(path, side=64)

    def test_a_64_px_k64_passes_through_when_128_px_is_the_default(self):
        data = bytes(64 * 64 * 3)
        self.assertEqual(kappdata.to_k64(self.k64(data)), data)

    def test_a_side_the_board_does_not_read_is_refused(self):
        with self.assertRaisesRegex(ValueError, "32"):
            kappdata.to_k64(self.png((100, 40), (255, 0, 0, 255)), side=32)


class FolderTests(unittest.TestCase):
    def test_folder_path(self):
        self.assertEqual(kappdata.folder("Scope"),
                         "/storage/.kode/appsdata/Scope")


ICON = bytes(range(256)) * (kappdata.K64_BYTES // 256)


class PackageCase(unittest.TestCase):
    """A scratch directory, and an app's `appdata/` folder written into it."""

    def setUp(self):
        self.tmp = tempfile.mkdtemp()
        self.addCleanup(_rmtree, self.tmp)

    def source(self, obj=None, files=None):
        src = os.path.join(self.tmp, "appdata")
        os.makedirs(src, exist_ok=True)
        if obj is not None:
            with open(os.path.join(src, "app.json"), "w") as fh:
                json.dump(obj, fh, indent=2)
        for name, body in (files or {}).items():
            with open(os.path.join(src, name), "wb") as fh:
                fh.write(body)
        return src

    def board_folder(self, files):
        out = os.path.join(self.tmp, "board")
        os.makedirs(out, exist_ok=True)
        for name, body in files.items():
            with open(os.path.join(out, name), "wb") as fh:
                fh.write(body)
        return out


class LoadTests(PackageCase):
    """load() turns an app's appdata/ folder into the two files the board stores."""

    def test_app_json_is_written_compact(self):
        src = self.source({"kode": "1", "description": "A test app", "color": "#112233"})
        body, icon = kappdata.load(src)
        self.assertEqual(body, b'{"kode":"1","description":"A test app","color":"#112233"}')
        self.assertEqual(icon, b"")

    def test_an_icon_found_is_named_icon_k64(self):
        src = self.source({"kode": "1", "icon": "art/mine.k64"}, {"icon.k64": ICON})
        os.mkdir(os.path.join(src, "art"))
        os.rename(os.path.join(src, "icon.k64"), os.path.join(src, "art", "mine.k64"))
        body, icon = kappdata.load(src)
        self.assertEqual(json.loads(body)["icon"], "icon.k64")
        self.assertEqual(icon, ICON)

    def test_icon_png_is_converted_without_being_named(self):
        from PIL import Image
        src = self.source({"kode": "1"})
        Image.new("RGBA", (128, 128), (0, 0, 255, 255)).save(os.path.join(src, "icon.png"))
        body, icon = kappdata.load(src)
        self.assertEqual(json.loads(body)["icon"], "icon.k64")
        self.assertEqual(len(icon), kappdata.K64_BYTES)
        self.assertEqual(len(icon), 49152)

    def test_icon_png_is_converted_to_the_64_px_form_when_asked(self):
        from PIL import Image
        src = self.source({"kode": "1"})
        Image.new("RGBA", (256, 256), (0, 0, 255, 255)).save(os.path.join(src, "icon.png"))
        body, icon = kappdata.load(src, side=64)
        self.assertEqual(json.loads(body)["icon"], "icon.k64")
        self.assertEqual(len(icon), 12288)

    def test_a_named_icon_that_is_not_there_is_refused(self):
        src = self.source({"kode": "1", "icon": "gone.png"})
        with self.assertRaisesRegex(ValueError, "gone.png"):
            kappdata.load(src)

    def test_a_folder_with_no_app_json_is_refused(self):
        with self.assertRaisesRegex(ValueError, "cannot read"):
            kappdata.load(self.source())

    def test_what_is_not_json_is_refused(self):
        src = self.source(files={"app.json": b"{not json"})
        with self.assertRaisesRegex(ValueError, "app.json"):
            kappdata.load(src)

    def test_an_app_json_over_the_boards_limit_is_refused(self):
        src = self.source({"kode": "1", "notes": "x" * kappdata.JSON_MAX})
        with self.assertRaisesRegex(ValueError, "longer than 1024"):
            kappdata.load(src)


class WriteTests(PackageCase):
    """write() leaves the board's folder: app.json, icon.k64 and nothing else."""

    def test_writes_the_two_files(self):
        out = os.path.join(self.tmp, "out", "welcome")
        kappdata.write(out, (b'{"kode":"1","icon":"icon.k64"}', ICON))
        self.assertEqual(sorted(os.listdir(out)), ["app.json", "icon.k64"])
        with open(os.path.join(out, "app.json"), "rb") as fh:
            self.assertEqual(fh.read(), b'{"kode":"1","icon":"icon.k64"}')
        with open(os.path.join(out, "icon.k64"), "rb") as fh:
            self.assertEqual(fh.read(), ICON)

    def test_what_an_older_build_left_goes(self):
        out = self.board_folder({"icon.k64": ICON, "stale.txt": b"x"})
        kappdata.write(out, (b'{"kode":"1"}', b""))
        self.assertEqual(os.listdir(out), ["app.json"])


class ReadTests(PackageCase):
    """read() takes a board folder back, and only a folder the board would read."""

    GOOD = b'{"kode":"1","icon":"icon.k64"}'

    def test_reads_back_what_write_left(self):
        out = os.path.join(self.tmp, "out")
        kappdata.write(out, (self.GOOD, ICON))
        self.assertEqual(kappdata.read(out), (self.GOOD, ICON))

    def test_a_folder_without_an_icon(self):
        out = self.board_folder({"app.json": b'{"kode":"1"}'})
        self.assertEqual(kappdata.read(out), (b'{"kode":"1"}', b""))

    def test_refuses_a_file_too_many(self):
        out = self.board_folder({"app.json": self.GOOD, "icon.k64": ICON, "notes.txt": b"x"})
        with self.assertRaisesRegex(ValueError, "notes.txt"):
            kappdata.read(out)

    def test_refuses_a_folder_with_no_app_json(self):
        out = self.board_folder({"icon.k64": ICON})
        with self.assertRaisesRegex(ValueError, "app.json"):
            kappdata.read(out)

    def test_refuses_an_icon_of_another_size(self):
        out = self.board_folder({"app.json": self.GOOD, "icon.k64": ICON[:-1]})
        with self.assertRaisesRegex(ValueError, "49151"):
            kappdata.read(out)

    def test_reads_an_icon_of_the_older_64_px_size(self):
        small = bytes(64 * 64 * 3)
        out = self.board_folder({"app.json": self.GOOD, "icon.k64": small})
        self.assertEqual(kappdata.read(out), (self.GOOD, small))

    def test_refuses_an_app_json_the_board_would_refuse(self):
        out = self.board_folder({"app.json": b'{"kode":"2"}'})
        with self.assertRaisesRegex(ValueError, "kode"):
            kappdata.read(out)

    def test_refuses_an_icon_the_app_json_does_not_name(self):
        out = self.board_folder({"app.json": b'{"kode":"1"}', "icon.k64": ICON})
        with self.assertRaisesRegex(ValueError, "icon"):
            kappdata.read(out)

    def test_refuses_an_app_json_that_names_an_icon_not_there(self):
        out = self.board_folder({"app.json": self.GOOD})
        with self.assertRaisesRegex(ValueError, "icon"):
            kappdata.read(out)


class DigestTests(unittest.TestCase):
    def test_is_the_sha256_of_the_two_files_in_order(self):
        self.assertEqual(kappdata.digest((b"{}", ICON)),
                         hashlib.sha256(b"{}" + ICON).hexdigest())

    def test_one_byte_of_either_file_changes_it(self):
        base = kappdata.digest((b'{"kode":"1"}', ICON))
        self.assertNotEqual(base, kappdata.digest((b'{"kode":"1" }', ICON)))
        self.assertNotEqual(base, kappdata.digest((b'{"kode":"1"}', ICON[::-1])))


class CommandLineTests(PackageCase):
    """`kappdata.py build` and `digest`, as the release's scripts run them."""

    TOOL = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))),
                        "kappdata.py")

    def run_tool(self, *args):
        return subprocess.run([sys.executable, self.TOOL, *args],
                              capture_output=True, text=True)

    def test_build_writes_the_folder_and_prints_its_digest(self):
        src = self.source({"kode": "1", "description": "A test app"}, {"icon.k64": ICON})
        out = os.path.join(self.tmp, "fat", "welcome")
        got = self.run_tool("build", src, out)
        self.assertEqual(got.returncode, 0, got.stderr)
        self.assertEqual(got.stdout.strip(), kappdata.digest(kappdata.read(out)))
        self.assertEqual(kappdata.read(out), kappdata.load(src))

    def test_digest_prints_the_same_for_the_folder_built(self):
        src = self.source({"kode": "1"}, {"icon.k64": ICON})
        out = os.path.join(self.tmp, "welcome")
        built = self.run_tool("build", src, out)
        again = self.run_tool("digest", out)
        self.assertEqual(again.returncode, 0, again.stderr)
        self.assertEqual(again.stdout, built.stdout)

    def test_build_writes_a_128_px_icon_by_default(self):
        from PIL import Image
        src = self.source({"kode": "1"})
        Image.new("RGBA", (256, 256), (0, 255, 0, 255)).save(os.path.join(src, "icon.png"))
        out = os.path.join(self.tmp, "welcome")
        got = self.run_tool("build", src, out)
        self.assertEqual(got.returncode, 0, got.stderr)
        self.assertEqual(os.path.getsize(os.path.join(out, "icon.k64")), 49152)

    def test_build_writes_the_64_px_form_with_icon_size_64(self):
        from PIL import Image
        src = self.source({"kode": "1"})
        Image.new("RGBA", (256, 256), (0, 255, 0, 255)).save(os.path.join(src, "icon.png"))
        out = os.path.join(self.tmp, "welcome")
        got = self.run_tool("build", src, out, "--icon-size", "64")
        self.assertEqual(got.returncode, 0, got.stderr)
        self.assertEqual(os.path.getsize(os.path.join(out, "icon.k64")), 12288)

    def test_build_refuses_an_icon_size_the_board_does_not_read(self):
        src = self.source({"kode": "1"})
        got = self.run_tool("build", src, os.path.join(self.tmp, "out"), "--icon-size", "32")
        self.assertEqual(got.returncode, 2)
        self.assertFalse(os.path.exists(os.path.join(self.tmp, "out")))

    def test_build_refuses_a_source_the_board_would_refuse(self):
        src = self.source({"kode": "2"})
        got = self.run_tool("build", src, os.path.join(self.tmp, "out"))
        self.assertEqual(got.returncode, 1)
        self.assertIn("kode", got.stderr)
        self.assertFalse(os.path.exists(os.path.join(self.tmp, "out")))

    def test_digest_refuses_a_folder_with_a_file_too_many(self):
        out = self.board_folder({"app.json": b'{"kode":"1"}', "notes.txt": b"x"})
        got = self.run_tool("digest", out)
        self.assertEqual(got.returncode, 1)
        self.assertIn("notes.txt", got.stderr)


if __name__ == "__main__":
    unittest.main(verbosity=2)
