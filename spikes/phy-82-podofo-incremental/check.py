#!/usr/bin/env python3
"""PHY-82 spike: apply each drawing variant to a copy of the signed fixture and
check the result. Throwaway code, see README.md.

usage: check.py <build-dir>   (run with the Python that has pyHanko, see
                               tests/fixtures/pdf/README.md)
"""

import re
import shutil
import subprocess
import sys
from pathlib import Path

from pyhanko.keys import load_cert_from_pemder
from pyhanko.pdf_utils.reader import PdfFileReader
from pyhanko.sign.validation import validate_pdf_signature
from pyhanko_certvalidator import ValidationContext

ROOT = Path(__file__).resolve().parents[2]
FIXTURES = ROOT / "tests/fixtures/pdf"
SIGNED = FIXTURES / "signed-a4.pdf"
CERT = FIXTURES / "signed-a4.cert.pem"

VARIANTS = [
    ("content", []),
    ("content", ["--no-metadata-update"]),
    ("content", ["--no-collect-garbage"]),
    ("content", ["--no-metadata-update", "--no-collect-garbage"]),
    ("annotation", []),
    ("annotation", ["--no-metadata-update"]),
    ("annotation", ["--no-collect-garbage"]),
    ("annotation", ["--no-metadata-update", "--no-collect-garbage"]),
]


def run(*args) -> subprocess.CompletedProcess:
    return subprocess.run([str(a) for a in args], capture_output=True, text=True)


def pyhanko_status(path: Path) -> dict:
    root = load_cert_from_pemder(str(CERT))
    with path.open("rb") as inf:
        signature = PdfFileReader(inf).embedded_signatures[0]
        status = validate_pdf_signature(signature, ValidationContext(trust_roots=[root]))
    return {
        "intact": status.intact,
        "valid": status.valid,
        "trusted": status.trusted,
        "coverage": status.coverage.name,
        "modification_level": status.modification_level.name if status.modification_level else None,
        "docmdp_ok": status.docmdp_ok,
        "bottom_line": status.bottom_line,
    }


def pdfsig_summary(path: Path) -> str:
    out = run("pdfsig", path).stdout
    lines = [line.strip() for line in out.splitlines()
             if line.strip().startswith(("- Signature Validation", "- Total document signed"))]
    return "; ".join(lines)


def appended_objects(path: Path) -> list[str]:
    """Object numbers and the start of their dictionary, in the new revision.
    Only top-level "N G obj" definitions: objects inside an object stream would be
    missed, so their presence is reported instead."""
    data = path.read_bytes()[SIGNED.stat().st_size:]
    found = []
    for match in re.finditer(rb"(\d+) (\d+) obj\s*<<(.{0,120}?)(?:>>|stream)", data, re.S):
        body = re.sub(rb"\s+", b" ", match.group(3)).decode("latin-1").strip()
        found.append(f"{match.group(1).decode()} {match.group(2).decode()}: << {body[:90]}")
    if re.search(rb"/Type\s*/ObjStm", data):
        found.append("warning: the revision has an object stream, whose objects are not listed")
    return found


def freed_objects(path: Path) -> str:
    """Free entries of the new revision's cross-reference table, other than the
    head of the free list (object 0)."""
    data = path.read_bytes()[SIGNED.stat().st_size:]
    start = data.rfind(b"\nxref")
    if start < 0:
        return "no classic xref table (cross-reference stream): not parsed"
    freed = []
    number = 0
    for line in data[start + 5:].split(b"trailer", 1)[0].splitlines():
        fields = line.split()
        if len(fields) == 2:  # subsection header: first object number, count
            number = int(fields[0])
        elif len(fields) == 3:
            if fields[2] == b"f" and number != 0:
                freed.append(f"{number} {int(fields[1])}")
            number += 1
    return ", ".join(freed) if freed else "none"


def main() -> int:
    build = Path(sys.argv[1]).resolve()
    out_dir = build / "out"
    shutil.rmtree(out_dir, ignore_errors=True)
    out_dir.mkdir()

    baseline = run(build / "render", SIGNED, out_dir / "baseline").stdout.split()
    print(f"baseline (signed-a4.pdf): pyHanko {pyhanko_status(SIGNED)['bottom_line']}, ink pixels {baseline}\n")

    for variant, options in VARIANTS:
        name = "".join([variant] + [o.replace("--no-", "-no-").replace("-update", "").replace("-garbage", "-gc")
                                    for o in options])
        target = out_dir / f"{name}.pdf"
        shutil.copyfile(SIGNED, target)
        draw = run(build / "draw", variant, target, *options)
        print(f"== {name}")
        if draw.returncode != 0:
            print(f"   draw failed: {draw.stderr.strip()}")
            continue
        qpdf = run("qpdf", "--check", target)
        print(f"   size: {SIGNED.stat().st_size} -> {target.stat().st_size} bytes, "
              f"revisions: {target.read_bytes().count(b'%%EOF')}, original bytes kept: "
              f"{target.read_bytes().startswith(SIGNED.read_bytes())}")
        print(f"   qpdf --check: exit {qpdf.returncode}")
        print(f"   pdfsig: {pdfsig_summary(target)}")
        print(f"   pyHanko: {pyhanko_status(target)}")
        print(f"   Qt PDF ink pixels (without, with annotations): {run(build / 'render', target, out_dir / name).stdout.strip()}")
        print(f"   freed objects (xref 'f' entries): {freed_objects(target)}")
        print("   new revision objects:")
        for obj in appended_objects(target):
            print(f"     {obj}")
        print()
    return 0


if __name__ == "__main__":
    sys.exit(main())
