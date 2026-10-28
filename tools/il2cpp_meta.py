#!/usr/bin/env python3
"""Minimal reader for IL2CPP global-metadata.dat (version 29, Unity 2021.3).

Lists images -> types -> methods (with param names) so we can figure out what
to hook without needing a third-party dumper. It can't resolve parameter/return
*types* - those live in GameAssembly.dll's Il2CppType tables - but names and
arity are enough to find things and to call il2cpp_class_get_method_from_name.

Usage:
  python tools/il2cpp_meta.py <global-metadata.dat> [--grep REGEX] [--image NAME]
  python tools/il2cpp_meta.py <global-metadata.dat> --summary

Reads the file only. Output goes to stdout; don't commit dumps of a commercial
game's metadata, keep them in local/.
"""
import argparse
import re
import struct
import sys

SANITY = 0xFAB11BAF

# v29 header: sanity, version, then 31 (offset, size) pairs in this exact order
HEADER_SECTIONS = [
    "stringLiteral", "stringLiteralData", "string", "events", "properties",
    "methods", "parameterDefaultValues", "fieldDefaultValues",
    "fieldAndParameterDefaultValueData", "fieldMarshaledSizes", "parameters",
    "fields", "genericParameters", "genericParameterConstraints",
    "genericContainers", "nestedTypes", "interfaces", "vtableMethods",
    "interfaceOffsets", "typeDefinitions", "images", "assemblies", "fieldRefs",
    "referencedAssemblies", "attributeData", "attributeDataRanges",
    "unresolvedVirtualCallParameterTypes", "unresolvedVirtualCallParameterRanges",
    "windowsRuntimeTypeNames", "windowsRuntimeStrings", "exportedTypeDefinitions",
]

# v29 layouts. sizes get checked against the sections, so a different version
# errors out instead of printing garbage
TYPE_DEF = struct.Struct("<IIiiiiiIiiiiiiii8HII")  # 88 bytes
METHOD_DEF = struct.Struct("<IiiiiIHHHH")           # 32 bytes
IMAGE_DEF = struct.Struct("<IiiIiIiIiI")            # 40 bytes
PARAM_DEF = struct.Struct("<IIi")                   # 12 bytes
FIELD_DEF = struct.Struct("<Iii")                   # 12 bytes
NESTED_DEF = struct.Struct("<i")                    # TypeDefinitionIndex


class MetadataError(Exception):
    pass


class Metadata:
    def __init__(self, data: bytes):
        if len(data) < 8:
            raise MetadataError("file too small to be global-metadata.dat")
        sanity, version = struct.unpack_from("<Ii", data, 0)
        if sanity != SANITY:
            raise MetadataError(f"bad magic 0x{sanity:08X} (encrypted or not a metadata file?)")
        if version != 29:
            raise MetadataError(f"metadata version {version}; this tool only knows v29")
        self.version = version
        self.data = data
        self.sec = {}
        for i, name in enumerate(HEADER_SECTIONS):
            off, size = struct.unpack_from("<ii", data, 8 + i * 8)
            if off < 0 or size < 0 or off + size > len(data):
                raise MetadataError(f"section {name} out of bounds")
            self.sec[name] = (off, size)
        for name, st in (("typeDefinitions", TYPE_DEF), ("methods", METHOD_DEF),
                         ("images", IMAGE_DEF), ("parameters", PARAM_DEF), ("fields", FIELD_DEF)):
            if self.sec[name][1] % st.size:
                raise MetadataError(f"{name} size not a multiple of {st.size}; layout mismatch")
        self._str_cache = {}

    def string(self, idx: int) -> str:
        s = self._str_cache.get(idx)
        if s is None:
            off = self.sec["string"][0] + idx
            end = self.data.index(b"\0", off)
            s = self.data[off:end].decode("utf-8", "replace")
            self._str_cache[idx] = s
        return s

    def _table(self, name, st):
        off, size = self.sec[name]
        return [st.unpack_from(self.data, off + i * st.size) for i in range(size // st.size)]

    def load(self):
        self.types = self._table("typeDefinitions", TYPE_DEF)
        self.methods = self._table("methods", METHOD_DEF)
        self.images = self._table("images", IMAGE_DEF)
        self.params = self._table("parameters", PARAM_DEF)
        self.fields = self._table("fields", FIELD_DEF)
        # declaringTypeIndex points into GameAssembly, can't read that from here.
        # nestedTypes is plain typedef indices though, so use that
        nested = self._table("nestedTypes", NESTED_DEF)
        self.outer = {}
        for ti, t in enumerate(self.types):
            start, count = t[12], t[20]
            if start < 0:
                continue
            for k in range(start, start + count):
                if 0 <= k < len(nested):
                    self.outer[nested[k][0]] = ti
        return self

    def type_name(self, ti: int) -> str:
        """'Ns.Outer/Inner' for nested types, same form as il2cpp_class_from_name wants"""
        t = self.types[ti]
        name = self.string(t[0])
        seen = {ti}
        cur = ti
        while cur in self.outer:
            cur = self.outer[cur]
            if cur in seen:  # corrupt data shouldn't hang the tool
                break
            seen.add(cur)
            name = f"{self.string(self.types[cur][0])}/{name}"
        ns = self.string(self.types[cur][1])
        return f"{ns}.{name}" if ns else name

    def iter_types(self):
        """yields (image_name, type_index) in image order"""
        for img in self.images:
            iname = self.string(img[0])
            start, count = img[2], img[3]
            for ti in range(start, start + count):
                yield iname, ti

    def type_methods(self, ti: int):
        t = self.types[ti]
        method_start, method_count = t[9], t[16]
        if method_start < 0:
            return
        for mi in range(method_start, method_start + method_count):
            m = self.methods[mi]
            pstart, pcount = m[3], m[9]
            pnames = [self.string(self.params[pi][0]) for pi in range(pstart, pstart + pcount)] if pstart >= 0 else []
            yield self.string(m[0]), pnames, m[6]  # name, param names, flags

    def type_fields(self, ti: int):
        t = self.types[ti]
        fstart, fcount = t[8], t[18]
        if fstart < 0:
            return
        for fi in range(fstart, fstart + fcount):
            yield self.string(self.fields[fi][0])


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("metadata")
    ap.add_argument("--grep", help="regex matched against 'Namespace.Type' and 'Type::Method'")
    ap.add_argument("--image", help="only list types from this image (e.g. Assembly-CSharp.dll)")
    ap.add_argument("--fields", action="store_true", help="also print field names")
    ap.add_argument("--summary", action="store_true", help="print counts per image and exit")
    a = ap.parse_args(argv)

    try:
        with open(a.metadata, "rb") as f:
            md = Metadata(f.read()).load()
    except (OSError, MetadataError) as e:
        print(f"error: {e}", file=sys.stderr)
        return 1

    out = sys.stdout
    if a.summary:
        print(f"metadata v{md.version}: {len(md.images)} images, {len(md.types)} types, {len(md.methods)} methods", file=out)
        for img in md.images:
            print(f"  {md.string(img[0])}: {img[3]} types", file=out)
        return 0

    rx = re.compile(a.grep, re.I) if a.grep else None
    for iname, ti in md.iter_types():
        if a.image and iname.lower() != a.image.lower():
            continue
        tname = md.type_name(ti)
        methods = list(md.type_methods(ti))
        if rx and not rx.search(tname):
            methods = [m for m in methods if rx.search(f"{tname}::{m[0]}")]
            if not methods:
                continue
        print(f"[{iname}] {tname}", file=out)
        if a.fields:
            for fn in md.type_fields(ti):
                print(f"    .{fn}", file=out)
        for name, pnames, flags in methods:
            static = "static " if flags & 0x10 else ""
            print(f"    {static}{name}({', '.join(pnames)})", file=out)
    return 0


if __name__ == "__main__":
    sys.exit(main())
