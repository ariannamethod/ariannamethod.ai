#!/usr/bin/env python3
"""Emit Unicode 15 Letter/Number ranges for codepoint_isalnum.

Development utility, with no runtime dependency. Input is the same pinned
UnicodeData.txt used by generate_text_lower_tables.py. The generated data uses
the Unicode license reproduced in core/ariannamethod.c and docs/TEXT_LOWER.md.
"""
import argparse
import hashlib
from pathlib import Path
import sys
import unicodedata

SHA256 = "806e9aed65037197f1ec85e12be6e8cd870fc5608b4de0fffd990f689f376a73"


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("unicode_data", type=Path)
    args = parser.parse_args()
    data = args.unicode_data.read_bytes()
    assert hashlib.sha256(data).hexdigest() == SHA256, "UnicodeData.txt hash"
    members = set()
    first = None
    for line in data.decode("utf-8").splitlines():
        row = line.split(";")
        cp, name, category = int(row[0], 16), row[1], row[2]
        if name.endswith(", First>"):
            first = (cp, category)
        elif name.endswith(", Last>"):
            assert first is not None and category == first[1]
            if category[0] in "LN":
                members.update(range(first[0], cp + 1))
            first = None
        elif category[0] in "LN":
            members.add(cp)
    assert first is None
    assert unicodedata.unidata_version == "15.0.0", unicodedata.unidata_version
    for cp in range(0x110000):
        assert (cp in members) == chr(cp).isalnum(), hex(cp)
    ranges = []
    for cp in sorted(members):
        if ranges and ranges[-1][1] + 1 == cp:
            ranges[-1][1] = cp
        else:
            ranges.append([cp, cp])
    assert len(members) == 137935 and len(ranges) == 747
    print("// Unicode 15.0 Letter/Number membership; generate_alnum_tables.py.")
    print("static const AM_UnicodeRange am_alnum_ranges[] = {")
    for pos in range(0, len(ranges), 4):
        print("    " + " ".join(f"{{0x{a:X},0x{b:X}}}," for a, b in ranges[pos:pos + 4]))
    print("};")
    print(f"Verified all 1,114,112 codepoints: {len(members):,} members / "
          f"{len(ranges)} ranges against Python {sys.version.split()[0]} / Unicode 15.",
          file=sys.stderr)


if __name__ == "__main__":
    main()
