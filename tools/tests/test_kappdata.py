#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""kappdata's pure half: app.json validation and icon conversion, no board.

`validate` is the same subset the board's own reader parses (the app.json keys
in https://docs.kode.diy/internals/apps-region); this pins the host and the device to one grammar by
testing the host's copy against the spec's own example and its own limits.
`to_k64` is tested against a PNG built on the fly with Pillow, so nothing
under tools/ needs a fixture image checked in - Pillow is the one dependency
that buys, and only this file and kappdata.py itself ever import it.

    python3 tools/tests/test_kappdata.py
"""

import os
import sys
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
    def test_converts_an_opaque_png(self):
        from PIL import Image
        import tempfile

        im = Image.new("RGBA", (100, 40), (255, 0, 0, 255))
        with tempfile.NamedTemporaryFile(suffix=".png", delete=False) as f:
            path = f.name
        self.addCleanup(os.remove, path)
        im.save(path)

        data = kappdata.to_k64(path)
        self.assertEqual(len(data), 64 * 64 * 2 + 64 * 64)
        alpha = data[64 * 64 * 2:]
        self.assertTrue(all(b == 255 for b in alpha))

    def test_k64_of_the_right_size_passes_through(self):
        import tempfile

        data = bytes(64 * 64 * 2 + 64 * 64)
        with tempfile.NamedTemporaryFile(suffix=".k64", delete=False) as f:
            f.write(data)
            path = f.name
        self.addCleanup(os.remove, path)
        self.assertEqual(kappdata.to_k64(path), data)

    def test_k64_of_the_wrong_size_is_refused(self):
        import tempfile

        with tempfile.NamedTemporaryFile(suffix=".k64", delete=False) as f:
            f.write(bytes(64 * 64 * 2 + 64 * 64 - 1))
            path = f.name
        self.addCleanup(os.remove, path)
        with self.assertRaisesRegex(ValueError, "icon: .* is 12287 bytes, a .k64 is 12288"):
            kappdata.to_k64(path)


class FolderTests(unittest.TestCase):
    def test_folder_path(self):
        self.assertEqual(kappdata.folder("Scope"),
                         "/storage/.kode/appsdata/Scope")


if __name__ == "__main__":
    unittest.main(verbosity=2)
