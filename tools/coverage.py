#!/usr/bin/env python3
"""Reset profiles, run the suite, and report coverage without hiding missing data."""
import argparse
import json
from pathlib import Path
import re
import subprocess
import sys


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("source", "build", "ctest", "gcovr", "gcov"):
        parser.add_argument("--" + name, required=True)
    parser.add_argument("--config", default="Debug")
    parser.add_argument("--min-line", type=float, default=0)
    parser.add_argument("--min-branch", type=float, default=0)
    args = parser.parse_args()
    source, build = Path(args.source).resolve(), Path(args.build).resolve()
    objects = build / "CMakeFiles"
    report = build / "coverage"
    report.mkdir(exist_ok=True)
    lock = build / ".baro-coverage-lock"
    try:
        lock.mkdir()
    except FileExistsError:
        parser.error(f"Coverage already running, or interrupted: inspect/remove {lock}")
    try:
        notes = sorted(objects.rglob("*.gcno"))
        if not notes:
            parser.error("No coverage notes: configure with BARO_COVERAGE=ON and build first")
        for data in objects.rglob("*.gcda"):
            data.unlink()
        tests = subprocess.run([args.ctest, "--test-dir", str(build), "-C", args.config,
                                "--output-on-failure", "-j", "1", "-E", "^discovery_multiconfig$"])
        missing = [str(note.relative_to(build)) for note in notes
                   if not note.with_suffix(".gcda").exists()]
        limitations = ("Abrupt exits, SIGABRT handlers, and killed processes may not flush "
                      "profiles. A shared object can have data from normal exits while losing "
                      "other subprocess contributions. Missing data is not proof of untested behavior. "
                      "This report covers this compiler/platform only; nested consumer builds "
                      "and the redundant multi-config integration run are not measured.")
        (report / "profiles.json").write_text(json.dumps({
            "notes": len(notes), "missing_profiles": missing, "limitations": limitations,
            "compiler_command": args.gcov, "tests_exit_status": tests.returncode,
        }, indent=2) + "\n")
        print("Coverage limitations:", limitations, flush=True)
        print(f"Profiles: {len(notes) - len(missing)}/{len(notes)} objects have data", flush=True)
        if len(missing) == len(notes):
            parser.error("No profiles were produced; refusing an empty coverage report")
        command = [args.gcovr, "--root", str(source), "--gcov-executable", args.gcov,
                   "--html-details", str(report / "index.html"),
                   "--json", str(report / "coverage.json"),
                   "--json-summary", str(report / "summary.json"),
                   "--xml", str(report / "coverage.xml"), "--txt", str(report / "coverage.txt"),
                   "--print-summary", "--fail-under-line", str(args.min_line),
                   "--fail-under-branch", str(args.min_branch)]
        for filename in ("baro.c", "baro_process.h", "baro_main.c", "baro_ctest.c"):
            command += ["--filter", re.escape(str(source / filename)) + "$" ]
        command.append(str(objects))
        result = subprocess.run(command)
        if result.returncode:
            return result.returncode
        summary = json.loads((report / "summary.json").read_text())
        measured = {entry["filename"] for entry in summary["files"]}
        if not {"baro.c", "baro_process.h", "baro_ctest.c"} <= measured:
            parser.error(f"Runtime coverage missing from report: {measured}")
        return tests.returncode
    finally:
        lock.rmdir()


if __name__ == "__main__":
    sys.exit(main())
