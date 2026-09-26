#!/usr/bin/env python3
"""Summarize a gcovr JSON summary as a Markdown table, one row per layer.

gcovr only summarizes per file or for the whole project; CI writes this table,
line and branch coverage per layer of src/ (domain, application, adapters, ui),
to the job summary. Files in subdirectories count toward their layer:

    gcovr build/coverage --json-summary build/coverage/summary.json
    coverage_summary.py build/coverage/summary.json >> "$GITHUB_STEP_SUMMARY"
"""

from __future__ import annotations

import argparse
import json
import sys
from collections import defaultdict
from pathlib import Path, PurePosixPath


def percent(covered: int, total: int) -> str:
    return f"{100 * covered / total:.1f} %" if total else "n/a"


def cell(covered: int, total: int) -> str:
    return f"{percent(covered, total)} ({covered}/{total})"


def layer(filename: str) -> str:
    """The first two path components, e.g. src/domain for src/domain/signature/x.cpp."""
    path = PurePosixPath(filename)
    return str(PurePosixPath(*path.parts[:2])) if len(path.parts) > 2 else str(path.parent)


def summarize(summary: dict) -> str:
    # [lines covered, lines total, branches covered, branches total] per layer.
    directories: dict[str, list[int]] = defaultdict(lambda: [0, 0, 0, 0])
    for entry in summary["files"]:
        directory = layer(entry["filename"])
        counts = directories[directory]
        counts[0] += entry["line_covered"]
        counts[1] += entry["line_total"]
        counts[2] += entry["branch_covered"]
        counts[3] += entry["branch_total"]

    rows = [
        "## Code coverage",
        "",
        "| Layer | Lines | Branches |",
        "|---|---:|---:|",
    ]
    for directory in sorted(directories):
        lines_covered, lines_total, branches_covered, branches_total = directories[directory]
        rows.append(
            f"| `{directory}` | {cell(lines_covered, lines_total)} "
            f"| {cell(branches_covered, branches_total)} |"
        )
    rows.append(
        f"| **Total** | **{cell(summary['line_covered'], summary['line_total'])}** "
        f"| **{cell(summary['branch_covered'], summary['branch_total'])}** |"
    )
    return "\n".join(rows) + "\n"


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("summary", type=Path, help="gcovr --json-summary output")
    args = parser.parse_args()
    sys.stdout.write(summarize(json.loads(args.summary.read_text(encoding="utf-8"))))
    return 0


if __name__ == "__main__":
    sys.exit(main())
