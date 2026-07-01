#!/usr/bin/env python3
"""Batch-tests a list of Fable item CDEFs against add_item_mod.

For each CDEF in the input list:
  1. Patches line 163 of add_item_mod.cpp with the new definition ID.
  2. Runs deploy.bat (builds the DLL, copies it + mods, launches Fable.exe).
  3. Waits for you to get into a game save, press F1 yourself, and press Enter
     here (synthetic key injection doesn't reach an elevated Fable process).
  4. Checks whether Fable.exe crashed and parses add_item_mod.log for the
     CreateFableItem / AddItemToInventory result lines.
  5. Force-closes Fable.exe (without saving) and appends a row to the results CSV.

Usage:
    python batch_test_cdefs.py --cdefs cdefs.txt --results results.csv

cdefs.txt format: one CDEF (int) per line, optional "id,name" CSV also accepted.
"""

import argparse
import csv
import re
import subprocess
import sys
import time
from pathlib import Path

REPO_DIR = Path(__file__).resolve().parent.parent
CPP_PATH = REPO_DIR / "mods" / "add_item_mod" / "add_item_mod.cpp"
DEPLOY_BAT = REPO_DIR / "deploy.bat"
CDEF_LINE_RE = re.compile(r"constexpr int kElixirOfLifeId = \d+;")


def patch_cdef(cdef: int):
    text = CPP_PATH.read_text()
    new_text, count = CDEF_LINE_RE.subn(
        f"constexpr int kElixirOfLifeId = {cdef};", text
    )
    if count != 1:
        raise RuntimeError(
            f"Expected exactly 1 match for the CDEF line in {CPP_PATH}, found {count}. "
            "Has the file changed shape?"
        )
    CPP_PATH.write_text(new_text)


def run_deploy():
    # deploy.bat builds, copies the DLL, and launches Fable.exe via `start` (non-blocking).
    # Output streams live to the console so build progress and any `pause` prompts are visible.
    result = subprocess.run(
        ["cmd.exe", "/c", str(DEPLOY_BAT)],
        cwd=REPO_DIR,
        timeout=300,
    )
    return result.returncode, "", ""


def is_fable_running() -> bool:
    out = subprocess.run(
        ["tasklist.exe", "/FI", "IMAGENAME eq Fable.exe"],
        capture_output=True,
        text=True,
    ).stdout
    return "Fable.exe" in out


def kill_fable():
    subprocess.run(
        ["taskkill.exe", "/IM", "Fable.exe", "/F"],
        capture_output=True,
        text=True,
    )


def read_cdefs(path: Path):
    cdefs = []
    for line in path.read_text().splitlines():
        line = line.strip()
        if not line or line.startswith("#"):
            continue
        parts = line.split(",")
        cdef = int(parts[0].strip())
        name = parts[1].strip() if len(parts) > 1 else ""
        cdefs.append((cdef, name))
    return cdefs


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--cdefs", required=True, type=Path, help="Text file with one CDEF per line (optionally 'id,name')")
    parser.add_argument("--results", required=True, type=Path, help="Output CSV path")
    parser.add_argument("--fable-dir", required=True, type=Path, help="Fable TLC install dir (must match deploy.bat's DEFAULT_DIR_FTLC_STEAM)")
    parser.add_argument("--boot-wait", type=float, default=15.0, help="Seconds to wait after launch before prompting you to press Enter (default 15)")
    parser.add_argument("--post-f1-wait", type=float, default=3.0, help="Seconds to wait after sending F1 before checking crash/log (default 3)")
    args = parser.parse_args()

    log_path = args.fable_dir / "mods" / "add_item_mod" / "add_item_mod.log"

    cdefs = read_cdefs(args.cdefs)
    print(f"Loaded {len(cdefs)} CDEFs to test.")

    write_header = not args.results.exists()
    with open(args.results, "a", newline="") as f:
        writer = csv.writer(f)
        if write_header:
            writer.writerow(["CDEF", "Name", "Crashed", "CreateItemOk", "AddItemResult", "LogTail"])

        for i, (cdef, name) in enumerate(cdefs, 1):
            print(f"\n[{i}/{len(cdefs)}] Testing CDEF {cdef} ({name or 'unnamed'}) ...")

            patch_cdef(cdef)
            rc, out, err = run_deploy()
            if rc != 0:
                print(f"  deploy.bat failed (exit {rc}):\n{err}")
                writer.writerow([cdef, name, "BUILD_FAILED", "", "", ""])
                f.flush()
                continue

            print(f"  Waiting {args.boot_wait}s for Fable to boot...")
            time.sleep(args.boot_wait)

            input("  Get into your test save, press F1 in-game yourself, then press Enter here... ")
            time.sleep(args.post_f1_wait)

            crashed = not is_fable_running()
            log_tail = ""
            create_ok = ""
            add_result = ""
            if log_path.exists():
                lines = log_path.read_text(errors="replace").splitlines()
                log_tail = " | ".join(lines[-5:])
                for line in lines:
                    if "CreateFableItem returned: 0x0" in line or "CreateFableItem failed" in line:
                        create_ok = "No"
                    elif "CreateFableItem returned: 0x" in line:
                        create_ok = "Yes"
                    m = re.search(r"AddItemToInventory returned (\d+)", line)
                    if m:
                        add_result = m.group(1)

            print(f"  Crashed: {crashed} | CreateItemOk: {create_ok or '?'} | AddItemResult: {add_result or '?'}")
            writer.writerow([cdef, name, "Yes" if crashed else "No", create_ok, add_result, log_tail])
            f.flush()

            if not crashed:
                kill_fable()
                time.sleep(1)

    print(f"\nDone. Results written to {args.results}")


if __name__ == "__main__":
    if sys.platform != "win32":
        print("This script must run on Windows (uses keybd_event, tasklist, taskkill).")
        sys.exit(1)
    main()
