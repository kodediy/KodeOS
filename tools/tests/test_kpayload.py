#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""kpayload's grammar half: quoting a string for the board, and reading a
record back, no board.

The board's own copy of these rules is console_grammar.c, and the host suite
holds that one to account byte by byte. This holds the host's copy to the
same rules - the three escapes and nothing else, a quoted value read back
whole - because the two drifting apart would show up as a file name that
arrives one byte different, which nothing on the wire would ever flag.

    python3 tools/tests/test_kpayload.py
"""

import hashlib
import json
import os
import sys
import tempfile
import unittest

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
from kpayload import (assets_plan, assets_refusal, fields, head_fields,  # noqa: E402
                      project_name, quote)


class Quote(unittest.TestCase):
    def test_wraps_and_escapes_the_three_bytes(self):
        self.assertEqual(quote("my notes.txt"), '"my notes.txt"')
        self.assertEqual(quote('a"b\\c\nd'), '"a\\"b\\\\c\\nd"')
        self.assertEqual(quote(""), '""')

    def test_everything_else_goes_out_as_it_is(self):
        self.assertEqual(quote("p@ss w=rd!\t#"), '"p@ss w=rd!\t#"')


class ProjectName(unittest.TestCase):
    """The name a bare image carries, which `install` sends as `name=`."""

    @staticmethod
    def image(name, magic=0xABCD5432, first=0xE9):
        img = bytearray(0x200)
        img[0] = first
        img[0x20:0x24] = magic.to_bytes(4, "little")
        img[0x50:0x70] = name.encode().ljust(32, b"\0")
        return bytes(img)

    def test_a_bare_image_gives_its_project_name(self):
        self.assertEqual(project_name(self.image("blink")), "blink")

    def test_a_name_that_fills_its_field_is_read_whole(self):
        self.assertEqual(project_name(self.image("n" * 32)), "n" * 32)

    def test_no_descriptor_means_no_name(self):
        self.assertEqual(project_name(self.image("blink", magic=0)), "")

    def test_a_container_or_a_short_file_has_no_name(self):
        self.assertEqual(project_name(self.image("blink", first=0x4B)), "")
        self.assertEqual(project_name(b"\xe9" * 16), "")


class AssetsRefusal(unittest.TestCase):
    """What the `assets_*` functions answer a refusal with, as records."""

    def test_a_refused_record_gives_its_why(self):
        recs = ['assets op=begin result=refused why=busy']
        self.assertEqual(assets_refusal(recs), "busy")

    def test_a_ready_record_is_no_refusal(self):
        self.assertIsNone(assets_refusal(['assets op=begin result=ready']))
        self.assertIsNone(assets_refusal([]))

    def test_a_file_refused_names_its_why_too(self):
        recs = ['assets file="a.json" result=ok depth=3',
                'assets file="b.json" result=refused why=size depth=0']
        self.assertEqual(assets_refusal(recs), "size")

    def test_other_records_are_left_alone(self):
        self.assertIsNone(assets_refusal(['scene name="tren" result=refused why=x']))


class Fields(unittest.TestCase):
    def test_bare_values_are_kept_as_they_are(self):
        self.assertEqual(fields("shot w=410 h=502 fmt=rgb565"),
                         {"w": "410", "h": "502", "fmt": "rgb565"})

    def test_a_quoted_value_is_read_back_whole(self):
        self.assertEqual(fields('file name="my notes = v2.txt" size=12 medium=sd'),
                         {"name": "my notes = v2.txt", "size": "12",
                          "medium": "sd"})

    def test_the_three_escapes_and_no_others(self):
        self.assertEqual(fields(r'x="a\"b\\c\nd\te"')["x"], 'a"b\\c\nd\\te')

    def test_an_empty_quoted_value_is_a_value(self):
        self.assertEqual(fields('text="" len=0'), {"text": "", "len": "0"})

    def test_a_bare_word_with_no_equals_is_not_a_pair(self):
        self.assertEqual(fields("pong"), {})
        self.assertEqual(head_fields(None), {})

    def test_whatever_quote_writes_fields_reads_back(self):
        nasty = '"\\\\n\\" = \t\n\\x '
        self.assertEqual(fields("v=" + quote(nasty) + " n=1"),
                         {"v": nasty, "n": "1"})


class AssetsPlan(unittest.TestCase):
    """What `assets-install DIR` sends: the manifest first, with every digest."""

    def pack(self, manifest, files):
        d = tempfile.TemporaryDirectory()
        self.addCleanup(d.cleanup)
        for name, data in files.items():
            with open(os.path.join(d.name, name), "wb") as f:
                f.write(data)
        with open(os.path.join(d.name, "kode.json"), "w") as f:
            json.dump(manifest, f)
        return d.name

    def test_the_manifest_goes_first_with_the_digests_added(self):
        d = self.pack({"format": 1, "scenes": [{"name": "a", "wake": "w.json", "loop": "l.json.z"}],
                       "loading": "w.json"},
                      {"w.json": b'{"w":410}', "l.json.z": b"zz"})
        plan = assets_plan(d)
        self.assertEqual([n for n, _ in plan], ["kode.json", "w.json", "l.json.z"])
        manifest = json.loads(plan[0][1])
        self.assertEqual(manifest["sha256"]["w.json"], hashlib.sha256(b'{"w":410}').hexdigest())
        self.assertEqual(manifest["sha256"]["l.json.z"], hashlib.sha256(b"zz").hexdigest())

    def test_a_reaction_list_is_sent_once_per_file(self):
        d = self.pack({"format": 1, "scenes": [{"name": "a", "wake": "w.json", "loop": "l.json",
                                                "on": {"look": ["x.json", "x.json"]}}]},
                      {"w.json": b"1", "l.json": b"2", "x.json": b"3"})
        self.assertEqual([n for n, _ in assets_plan(d)], ["kode.json", "w.json", "l.json", "x.json"])

    def test_a_digest_that_does_not_match_is_refused(self):
        d = self.pack({"format": 1, "scenes": [{"name": "a", "wake": "w.json", "loop": "l.json"}],
                       "sha256": {"w.json": "00" * 32}},
                      {"w.json": b"x", "l.json": b"y"})
        with self.assertRaises(SystemExit):
            assets_plan(d)

    def test_a_missing_file_is_refused(self):
        d = self.pack({"format": 1, "scenes": [{"name": "a", "wake": "w.json", "loop": "l.json"}]},
                      {"w.json": b"x"})
        with self.assertRaises(SystemExit):
            assets_plan(d)

    def test_a_path_or_a_long_name_is_refused(self):
        d = self.pack({"format": 1, "scenes": [{"name": "a", "wake": "sub/w.json", "loop": "l.json"}]},
                      {"l.json": b"y"})
        with self.assertRaises(SystemExit):
            assets_plan(d)
        long = "a" * 36 + ".json"
        d = self.pack({"format": 1, "scenes": [{"name": "a", "wake": long, "loop": "l.json"}]},
                      {long: b"x", "l.json": b"y"})
        with self.assertRaises(SystemExit):
            assets_plan(d)


if __name__ == "__main__":
    unittest.main()
