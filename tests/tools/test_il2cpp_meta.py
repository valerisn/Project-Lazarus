"""Tests for tools/il2cpp_meta.py against hand-built metadata blobs (no game files)."""
import io
import os
import struct
import sys
import tempfile
import unittest
from contextlib import redirect_stdout

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "..", "tools"))
import il2cpp_meta as m  # noqa: E402


def build_blob(version=29):
    """Ns.Outer with nested Inner, and Inner has nested Deeper. one image."""
    strings = b"\0Ns\0Outer\0Inner\0Deeper\0Game.dll\0"
    idx = {name: strings.index(name.encode() + b"\0") for name in ("Ns", "Outer", "Inner", "Deeper", "Game.dll")}
    empty = 0  # offset 0 of the string table is ""

    def typedef(name, ns, nested_start, nested_count):
        return m.TYPE_DEF.pack(idx[name], ns, 0, -1, -1, -1, -1, 0,
                               -1, -1, -1, -1, nested_start, -1, -1, -1,
                               0, 0, 0, 0, nested_count, 0, 0, 0, 0, 0)

    types = typedef("Outer", idx["Ns"], 0, 1) + typedef("Inner", empty, 1, 1) + typedef("Deeper", empty, -1, 0)
    nested = struct.pack("<ii", 1, 2)  # Outer -> Inner, Inner -> Deeper
    images = m.IMAGE_DEF.pack(idx["Game.dll"], 0, 0, 3, -1, 0, -1, 0, 0, 0)

    header_size = 8 + 8 * len(m.HEADER_SECTIONS)
    body = {}
    cursor = header_size
    blob_parts = []
    for name, data in (("string", strings), ("typeDefinitions", types), ("nestedTypes", nested), ("images", images)):
        body[name] = (cursor, len(data))
        blob_parts.append(data)
        cursor += len(data)
    header = struct.pack("<Ii", m.SANITY, version)
    for name in m.HEADER_SECTIONS:
        off, size = body.get(name, (header_size, 0))
        header += struct.pack("<ii", off, size)
    return header + b"".join(blob_parts)


class MetaTests(unittest.TestCase):
    def test_nested_names(self):
        md = m.Metadata(build_blob()).load()
        self.assertEqual(md.type_name(0), "Ns.Outer")
        self.assertEqual(md.type_name(1), "Ns.Outer/Inner")
        self.assertEqual(md.type_name(2), "Ns.Outer/Inner/Deeper")

    def test_cli_grep_matches_nested(self):
        with tempfile.TemporaryDirectory() as d:
            p = os.path.join(d, "global-metadata.dat")
            with open(p, "wb") as f:
                f.write(build_blob())
            out = io.StringIO()
            with redirect_stdout(out):
                rc = m.main([p, "--grep", "Inner/Deeper"])
            self.assertEqual(rc, 0)
            self.assertIn("[Game.dll] Ns.Outer/Inner/Deeper", out.getvalue())

    def test_rejects_wrong_version_and_magic(self):
        with self.assertRaises(m.MetadataError):
            m.Metadata(build_blob(version=27))
        with self.assertRaises(m.MetadataError):
            m.Metadata(b"\0" * 300)

    def test_cycle_in_nested_table_does_not_hang(self):
        md = m.Metadata(build_blob()).load()
        md.outer[0] = 2  # corrupt: Outer nested in its own grandchild
        self.assertTrue(md.type_name(2).endswith("Deeper"))


if __name__ == "__main__":
    unittest.main()
