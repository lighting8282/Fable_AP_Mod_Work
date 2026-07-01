#!/usr/bin/env python3
"""Matches cdefs.txt (ID,SymbolName) entries against the item names in the
TSV item list, to auto-fill CDEFs for confident name matches and leave the
rest for the manual F1 crash-test.

Usage:
    python match_cdefs_to_items.py --cdefs cdefs.txt --tsv "items.tsv" --output matches.tsv

TSV is expected to have columns: Category, Item, CDEF, Inventory?, Comments
(header row required). Only rows with an empty CDEF are matched.
"""

import argparse
import csv
import re
from pathlib import Path

STRIP_PREFIXES = ("OBJECT_", "CREATURE_", "MELEE_", "RANGED_")


def normalize(name: str) -> str:
    name = name.upper()
    for prefix in STRIP_PREFIXES:
        if name.startswith(prefix):
            name = name[len(prefix):]
    name = re.sub(r"_0*\d+$", "", name)  # drop trailing _01, _02 variant suffixes
    name = re.sub(r"[^A-Z0-9]", "", name)
    return name


def load_cdefs(path: Path):
    entries = []
    for line in path.read_text().splitlines():
        line = line.strip()
        if not line or line.startswith("#"):
            continue
        cdef_str, _, name = line.partition(",")
        entries.append((int(cdef_str), name))
    return entries


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--cdefs", required=True, type=Path)
    parser.add_argument("--tsv", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()

    cdefs = load_cdefs(args.cdefs)
    norm_to_cdefs = {}
    for cdef, name in cdefs:
        norm_to_cdefs.setdefault(normalize(name), []).append((cdef, name))

    with open(args.tsv, encoding="utf-8") as f:
        reader = csv.reader(f, delimiter="\t")
        header = next(reader)
        rows = list(reader)

    out_rows = []
    matched = 0
    for row in rows:
        row = row + [""] * (len(header) - len(row))
        category, item, cdef_col = row[0], row[1], row[2] if len(row) > 2 else ""
        if cdef_col.strip():
            out_rows.append(row + ["already set"])
            continue

        item_norm = normalize(item)
        candidates = norm_to_cdefs.get(item_norm, [])

        if len(candidates) == 1:
            cdef, symbol = candidates[0]
            row = list(row)
            row[2] = str(cdef)
            out_rows.append(row + [f"AUTO-MATCHED: {symbol}"])
            matched += 1
        elif len(candidates) > 1:
            options = "; ".join(f"{c}={n}" for c, n in candidates)
            out_rows.append(row + [f"AMBIGUOUS ({len(candidates)} candidates): {options}"])
        else:
            out_rows.append(row + ["NO MATCH -- needs F1 crash-test"])

    with open(args.output, "w", newline="", encoding="utf-8") as f:
        writer = csv.writer(f, delimiter="\t")
        writer.writerow(header + ["MatchNotes"])
        writer.writerows(out_rows)

    print(f"{matched} item(s) auto-matched by exact normalized name.")
    print(f"Wrote {len(out_rows)} rows to {args.output}")


if __name__ == "__main__":
    main()
