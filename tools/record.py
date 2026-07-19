#!/usr/bin/env python3
"""Record a verified CDEF -> in-game name mapping into the items TSV.

Usage:
    python tools/record.py <cdef> <symbol> <in-game name> [--cat CATEGORY]

If a sheet row with that name exists and is not already verified, it is filled in.
Otherwise the item is appended as a BONUS row (an item the original sheet lacks).

    python tools/record.py 4485 OBJECT_HOUSE_DEEDS "House Deeds" --cat Quest
"""
import csv
import os
import sys

TSV = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))),
                   "data", "Fable lists for AP - Items.tsv")


def main(argv):
    if len(argv) < 3:
        print(__doc__)
        return 1
    cdef, symbol = argv[0], argv[1]
    cat = "Quest"
    rest = argv[2:]
    if "--cat" in rest:
        i = rest.index("--cat")
        cat = rest[i + 1]
        rest = rest[:i] + rest[i + 2:]
    name = " ".join(rest).strip()

    rows = list(csv.reader(open(TSV, encoding="utf-8"), delimiter="\t"))
    header, body = rows[0], [r + [""] * (5 - len(r)) for r in rows[1:]]

    note = ("Confirmed via clean single-CDEF isolated test (fresh profile). "
            f"Symbol {symbol}.")

    hit = [r for r in body
           if r[1].strip().lower() == name.lower() and r[3].strip() != "Yes"]
    if hit:
        hit[0][2], hit[0][3], hit[0][4] = cdef, "Yes", note
        print(f"ON SHEET  -> {name} = {cdef}  (category {hit[0][0]})")
    else:
        body.append([cat, f"{name} (BONUS - not on original sheet)", cdef, "Yes",
                     note + " Adds cleanly. Not on the original sheet."])
        print(f"BONUS     -> {name} = {cdef}  (category {cat})")

    csv.writer(open(TSV, "w", newline="", encoding="utf-8"),
               delimiter="\t").writerows([header] + body)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
