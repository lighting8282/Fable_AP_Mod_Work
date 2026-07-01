#!/usr/bin/env python3
"""Parses "ID - SYMBOL_NAME" lines (as seen in FESN's OBJECT tree) from a text
dump and appends new (unique) entries to a CDEF list CSV usable by
batch_test_cdefs.py.

Paste/OCR'd screenshot text almost always contains this pattern per line:
    1186 - OBJECT_SUMMONER_LIGHTNING_ORB_BASE
Lines that don't match (e.g. "125 - NULLDEF_OBJECT") are still captured --
filtering NULLDEF/TEMPLATE placeholders is left to the CSV review step, since
some of them are legitimately worth testing for name-mismatch cases.

Usage:
    python parse_fesn_screenshot_dump.py --input dump.txt --output cdefs.txt
    (or pipe text on stdin with --input -)
"""

import argparse
import re
import sys
from pathlib import Path

LINE_RE = re.compile(r"(\d+)\s*-\s*([A-Za-z0-9_.]+)")


def parse(text: str):
    seen = {}
    for line in text.splitlines():
        m = LINE_RE.search(line.strip())
        if not m:
            continue
        cdef = int(m.group(1))
        name = m.group(2)
        seen[cdef] = name  # last write wins if duplicated across pasted batches
    return seen


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--input", required=True, help="Text file with pasted FESN tree dump, or '-' for stdin")
    parser.add_argument("--output", required=True, type=Path, help="cdefs.txt to append/merge into (id,name per line)")
    args = parser.parse_args()

    text = sys.stdin.read() if args.input == "-" else Path(args.input).read_text(errors="replace")
    new_entries = parse(text)

    existing = {}
    if args.output.exists():
        for line in args.output.read_text().splitlines():
            line = line.strip()
            if not line or line.startswith("#"):
                continue
            parts = line.split(",", 1)
            existing[int(parts[0])] = parts[1] if len(parts) > 1 else ""

    added = 0
    for cdef, name in new_entries.items():
        if cdef not in existing:
            added += 1
        existing[cdef] = name

    with open(args.output, "w") as f:
        for cdef in sorted(existing):
            f.write(f"{cdef},{existing[cdef]}\n")

    print(f"Parsed {len(new_entries)} entries from input, {added} new, {len(existing)} total in {args.output}")


if __name__ == "__main__":
    main()
