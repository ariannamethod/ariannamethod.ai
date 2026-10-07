#!/usr/bin/env python3
"""Direct CPython 3.12 / Unicode 15 oracle for AML am_string_lower.

Usage: python3 verify_text_lower.py --library ./lower_probe.so
No Unicode mapping table is consulted by this oracle. Its expected values come
from str.lower(). Scalars are batched as LF-separated records; all AML-valid
scalars are checked in five contexts, including both sigma directions.
"""
import argparse
import ctypes
import hashlib
import json
from pathlib import Path
import random
import sys
import time
import unicodedata


class String(ctypes.Structure):
    _fields_ = [("data", ctypes.c_void_p), ("byte_len", ctypes.c_int),
                ("len", ctypes.c_int), ("refcount", ctypes.c_int)]


CURATED = [
    "", "A Simple ORGANISM", "already small", "123!?", "ПрИвЕт, ЁЖ",
    "ÉTÉ À PARIS", "שלום עולם", "اللغة العربية", "漢字", "🦋💭",
    "İ", "Iİıi", "İI\u0307", "ẞß", "KÅΩ", "ǅǈǋǲ", "ǰﬀﬁﬂﬃﬄﬅﬆ",
    "Σ", "ΑΣ", "ΣΑ", "ΑΣΑ", "ΣΣ", "ΣΣΣ", "ΑΣ ΣΑ", "ΑΣ\nΑ",
    "AΣ\u0301", "AΣ\u0301A", "A\u0301Σ", "\u0301Σ", "\u0345Σ",
    "A\u0345Σ", "AΣ\u0345", "AΣ\u0345A", "AΣ\u0345!",
    "AΣ'A", "AΣ’Α", "AΣ:A", "AΣ.A", "AΣ-A", "AΣ_A",
    "AΣ\u200dA", "AΣ\ufe0fA", "AΣ\U000e0100A", "AΣ\u00adA",
    "AΣ\u02b0", "AΣ\u02b0A", "\u02b0Σ", "A\u02b0Σ",
    "AΣ\u2160", "\u2160Σ", "AΣ\U0001d400", "\U0001d400Σ",
    "\U00010400\U00010427", "\U000104b0\U000104d3",
    "\U00010c80\U00010cb2", "\U000118a0\U000118bf", "\U00016e40\U00016e5f",
    "\U0001e900\U0001e921", "ᎠᏵꭰꮿ", "ᲐᲿႠჅ", "ȺȾⱥⱦ", "\U0010ffff",
]


def bind(path):
    lib = ctypes.CDLL(str(path.resolve()))
    lib.am_string_new.argtypes, lib.am_string_new.restype = [ctypes.c_char_p], ctypes.POINTER(String)
    lib.am_string_lower.argtypes, lib.am_string_lower.restype = [ctypes.POINTER(String)], ctypes.POINTER(String)
    lib.am_string_free.argtypes, lib.am_string_free.restype = [ctypes.POINTER(String)], None
    return lib


def check(lib, text):
    data, expected = text.encode("utf-8"), text.lower()
    source = lib.am_string_new(data)
    assert source, ("source allocation", len(data))
    result = None
    try:
        result = lib.am_string_lower(source)
        assert result, ("lower returned NULL", len(data))
        actual = ctypes.string_at(result.contents.data, result.contents.byte_len)
        expected_bytes = expected.encode("utf-8")
        if actual != expected_bytes:
            pos = next((i for i, (a, b) in enumerate(zip(actual, expected_bytes)) if a != b), min(len(actual), len(expected_bytes)))
            raise AssertionError(("lower mismatch", pos, actual[max(0, pos-30):pos+30], expected_bytes[max(0, pos-30):pos+30]))
        assert result.contents.len == len(expected)
        assert result.contents.refcount == 1
        assert ctypes.addressof(source.contents) != ctypes.addressof(result.contents)
        assert ctypes.string_at(source.contents.data, source.contents.byte_len) == data
        return actual
    finally:
        if result:
            lib.am_string_free(result)
        lib.am_string_free(source)


def scalar_batches(pattern):
    batch = []
    count = 0
    for cp in range(1, 0x110000):
        if 0xD800 <= cp <= 0xDFFF:
            continue
        batch.append(pattern(chr(cp)))
        count += 1
        if len(batch) == 8192:
            yield "".join(batch), count
            batch, count = [], 0
    if batch:
        yield "".join(batch), count


def c_quote(text):
    # Octal escapes have fixed width and cannot swallow following hex digits.
    return '"' + "".join("\\%03o" % b for b in text.encode("utf-8")) + '"'


def main():
    parser = argparse.ArgumentParser(__doc__)
    parser.add_argument("--library", required=True, type=Path)
    parser.add_argument("--write-fixtures", type=Path)
    args = parser.parse_args()
    assert unicodedata.unidata_version == "15.0.0", unicodedata.unidata_version
    lib = bind(args.library)
    started = time.monotonic()
    for text in CURATED:
        check(lib, text)
    patterns = [lambda c: c + "\n", lambda c: c + "Σ\n",
                lambda c: "A" + c + "Σ\n", lambda c: "AΣ" + c + "\n",
                lambda c: "AΣ" + c + "A\n"]
    digests, total = [], 0
    for mode, pattern in enumerate(patterns):
        digest = hashlib.sha256()
        checked = 0
        for text, count in scalar_batches(pattern):
            digest.update(check(lib, text))
            checked += count
        digests.append(digest.hexdigest())
        total += checked
        print("mode", mode, "checked", checked, "sha256", digests[-1], flush=True)
    # Long case-ignorable spans exercise the linear context traversal and byte
    # boundaries; the same property also occurs in supplementary codepoints.
    for text in ("AΣ" + "\u0345" * 100000, "AΣ" + "\u0345" * 100000 + "A",
                 "A\u0345Σ" * 50000, "\U0001d400Σ\U000e0100" * 20000):
        check(lib, text)
    rng = random.Random(401570)
    alphabet = "AΣİ' .\n\u0345\u0301\u02b0\u200d\U0001d400\U000e0100"
    for _ in range(200):
        check(lib, "".join(rng.choice(alphabet) for _ in range(500)))
    if args.write_fixtures:
        lines = ["/* Generated directly by Python %s / Unicode %s str.lower(). */" % (
                    sys.version.split()[0], unicodedata.unidata_version),
                 "static const struct { const char* source; const char* expected; } lower_cases[] = {"]
        lines += ["    {%s, %s}," % (c_quote(s), c_quote(s.lower())) for s in CURATED]
        lines.append("};")
        args.write_fixtures.write_text("\n".join(lines) + "\n")
    print(json.dumps({"python": sys.version.split()[0], "unicode": unicodedata.unidata_version,
        "scalar_context_cases": total, "curated": len(CURATED), "long": 4,
        "random_strings": 200, "elapsed_seconds": round(time.monotonic() - started, 3),
        "output_sha256": digests}, indent=2))


if __name__ == "__main__":
    main()
