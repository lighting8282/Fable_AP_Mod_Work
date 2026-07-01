import csv

with open("cdefs.txt") as f, open(r"A:\Archipelago\Games\Fable\OBJECT_tree_full_list.tsv", "w", newline="", encoding="utf-8") as out:
    writer = csv.writer(out, delimiter="\t")
    writer.writerow(["CDEF", "SymbolName"])
    for line in f:
        line = line.strip()
        if not line:
            continue
        cdef, _, name = line.partition(",")
        writer.writerow([cdef, name])
