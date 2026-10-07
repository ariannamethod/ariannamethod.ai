#!/usr/bin/env python3
"""Generate AML Unicode 15.0.0 lowercase data from pinned Unicode files.

Development utility; it adds no runtime dependency. Download UnicodeData.txt,
SpecialCasing.txt, DerivedCoreProperties.txt from
https://www.unicode.org/Public/15.0.0/ucd/ into --ucd-dir first.
"""
import argparse
import ctypes
import hashlib
from pathlib import Path
import sys
import unicodedata

HASHES = {
    "UnicodeData.txt": "806e9aed65037197f1ec85e12be6e8cd870fc5608b4de0fffd990f689f376a73",
    "SpecialCasing.txt": "78b29c64b5840d25c11a9f31b665ee551b8a499eca6c70d770fcad7dd710f494",
    "DerivedCoreProperties.txt": "d367290bc0867e6b484c68370530bdd1a08b6b32404601b8c7accaf83e05628d",
}


def read_pinned(directory, name):
    data = (directory / name).read_bytes()
    assert hashlib.sha256(data).hexdigest() == HASHES[name], name
    return data.decode("utf-8")


def intervals(values):
    result = []
    for cp in sorted(values):
        if result and cp == result[-1][1] + 1:
            result[-1][1] = cp
        else:
            result.append([cp, cp])
    return result


def mapping_ranges(mapping):
    pairs = sorted((cp, out[0] - cp) for cp, out in mapping.items()
                   if len(out) == 1 and cp != out[0])
    result = []
    i = 0
    while i < len(pairs):
        first, delta = pairs[i]
        last, stride = first, 1
        if (i + 1 < len(pairs) and pairs[i + 1][1] == delta
                and pairs[i + 1][0] - first in (1, 2)):
            stride = pairs[i + 1][0] - first
            while i + 1 < len(pairs) and pairs[i + 1] == (last + stride, delta):
                i += 1
                last = pairs[i][0]
        result.append([first, last, delta, stride])
        i += 1
    return result


def source_data(directory):
    mapping = {}
    for line in read_pinned(directory, "UnicodeData.txt").splitlines():
        row = line.split(";")
        if row[13]:
            mapping[int(row[0], 16)] = (int(row[13], 16),)
    contextual = []
    for line in read_pinned(directory, "SpecialCasing.txt").splitlines():
        row = [x.strip() for x in line.split("#", 1)[0].split(";")]
        if len(row) < 5:
            continue
        cp = int(row[0], 16)
        if not row[4]:
            mapping[cp] = tuple(int(x, 16) for x in row[1].split())
        elif row[4] == "Final_Sigma":
            contextual.append((cp, row[1]))
        else:
            assert row[4].split()[0] in ("tr", "az", "lt"), row
    assert contextual == [(0x3A3, "03C2")]
    assert {cp: out for cp, out in mapping.items() if len(out) != 1} == {
        0x130: (0x69, 0x307)}
    props = {"Cased": set(), "Case_Ignorable": set()}
    for line in read_pinned(directory, "DerivedCoreProperties.txt").splitlines():
        row = [x.strip() for x in line.split("#", 1)[0].split(";")]
        if len(row) != 2 or row[1] not in props:
            continue
        edges = row[0].split("..")
        first, last = int(edges[0], 16), int(edges[-1], 16)
        props[row[1]].update(range(first, last + 1))
    return mapping, props


def verify_python(mapping, props):
    assert unicodedata.unidata_version == "15.0.0", unicodedata.unidata_version
    for cp in range(0x110000):
        assert tuple(map(ord, chr(cp).lower())) == mapping.get(cp, (cp,)), hex(cp)
    for name, symbol in (("Cased", "_PyUnicode_IsCased"),
                         ("Case_Ignorable", "_PyUnicode_IsCaseIgnorable")):
        func = getattr(ctypes.pythonapi, symbol)
        func.argtypes, func.restype = [ctypes.c_uint], ctypes.c_int
        for cp in range(0x110000):
            assert bool(func(cp)) == (cp in props[name]), (name, hex(cp))
    print("Verified all 1,114,112 codepoints against Python " + sys.version.split()[0]
          + " / Unicode " + unicodedata.unidata_version, file=sys.stderr)


def render(mapping, props):
    lines = [
        "// Generated from Unicode 15.0.0 UnicodeData, SpecialCasing, and",
        "// DerivedCoreProperties. Regenerate with generate_text_lower_tables.py.",
        "// Simple mappings use stride 1 or 2; unlisted scalars map to themselves.",
        "typedef struct { uint32_t first, last; int32_t delta; uint32_t stride; } AM_LowerRange;",
        "typedef struct { uint32_t first, last; } AM_UnicodeRange;",
        "static const AM_LowerRange am_lower_ranges[] = {",
    ]
    rows = ["{0x%Xu, 0x%Xu, %d, %du}" % tuple(row)
            for row in mapping_ranges(mapping)]
    lines.extend("    " + ", ".join(rows[i:i + 3]) + ","
                 for i in range(0, len(rows), 3))
    lines.append("};")
    for prop, name in (("Cased", "am_cased_ranges"),
                       ("Case_Ignorable", "am_case_ignorable_ranges")):
        rows = ["{0x%Xu, 0x%Xu}" % tuple(row) for row in intervals(props[prop])]
        lines.append("static const AM_UnicodeRange " + name + "[] = {")
        lines.extend("    " + ", ".join(rows[i:i + 4]) + ","
                     for i in range(0, len(rows), 4))
        lines.append("};")
    return "\n".join(lines) + "\n"


def main():
    parser = argparse.ArgumentParser(__doc__)
    parser.add_argument("--ucd-dir", required=True, type=Path)
    parser.add_argument("--verify-python", action="store_true")
    args = parser.parse_args()
    mapping, props = source_data(args.ucd_dir)
    if args.verify_python:
        verify_python(mapping, props)
    sys.stdout.write(render(mapping, props))
    print("Data: %d mapping ranges, %d Cased intervals, %d Case_Ignorable intervals" % (
        len(mapping_ranges(mapping)), len(intervals(props["Cased"])),
        len(intervals(props["Case_Ignorable"]))), file=sys.stderr)


if __name__ == "__main__":
    main()
