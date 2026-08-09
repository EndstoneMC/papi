#!/usr/bin/env python3
"""Run clang-tidy on project-owned translation units.

Reads the CMake compilation database (compile_commands.json) and runs clang-tidy on
every source file under src/ and tests/. Failures are reported with their diagnostics.
"""

from __future__ import annotations

import argparse
import concurrent.futures
import json
import os
import pathlib
import re
import subprocess
import sys


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Run clang-tidy on project-owned translation units")
    parser.add_argument("--build-dir", required=True, type=pathlib.Path)
    parser.add_argument("--clang-tidy", default="clang-tidy")
    parser.add_argument("--jobs", type=int, default=min(4, max(1, os.cpu_count() or 1)))
    parser.add_argument("--fix", action="store_true")
    parser.add_argument("--summary", action="store_true")
    parser.add_argument("--match")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    root = pathlib.Path(__file__).resolve().parents[1]
    build_dir = args.build_dir.resolve()
    database_path = build_dir / "compile_commands.json"
    with database_path.open(encoding="utf-8") as database_file:
        database = json.load(database_file)

    sources: set[pathlib.Path] = set()
    for entry in database:
        source = pathlib.Path(entry["file"]).resolve()
        try:
            relative = source.relative_to(root)
        except ValueError:
            continue
        if (
            relative.parts
            and relative.parts[0] in {"src", "tests"}
            and (args.match is None or re.search(args.match, relative.as_posix()))
        ):
            sources.add(source)

    def check(source: pathlib.Path) -> tuple[pathlib.Path, subprocess.CompletedProcess[str]]:
        root_pattern = re.escape(root.as_posix())
        header_filter = rf"^{root_pattern}/(?:src|tests)/"
        command = [args.clang_tidy, "--quiet", f"--header-filter={header_filter}", "-p", str(build_dir), str(source)]
        if args.fix:
            command.append("--fix-errors")
        return source, subprocess.run(command, capture_output=True, text=True, check=False)

    failures = []
    with concurrent.futures.ThreadPoolExecutor(max_workers=args.jobs) as executor:
        for source, result in executor.map(check, sorted(sources)):
            if result.returncode != 0:
                failures.append(source)
                if args.summary:
                    diagnostics = sorted(set(re.findall(r"\[([^,\]]+)(?:,-warnings-as-errors)?\]", result.stdout)))
                    print(f"{source.relative_to(root)}: {', '.join(diagnostics)}")
                else:
                    sys.stdout.write(result.stdout)

    if failures:
        print(f"\n{len(failures)} file(s) had clang-tidy errors.", file=sys.stderr)
        return 1
    print(f"OK: {len(sources)} file(s) checked, no clang-tidy errors.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
