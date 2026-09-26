#!/usr/bin/env python3
"""Generate the reference PDF corpus used by the tests.

Every file in this directory is produced by this script, so the corpus can be
redistributed under the project license. See README.md for what each file covers.

The geometry fixtures are written by hand with the standard library only: their
bytes are deterministic. Two files need external tools, and change on every run:

- signed-a4.pdf and the certified-p*-a4.pdf files are signed with pyHanko
  (`pip install -r requirements.txt`);
- pdfa-2b-a4.pdf is converted to PDF/A-2b with Ghostscript (`gs`).

Every page draws, upright as displayed once /Rotate is applied: a frame along the
CropBox, the fixture name, its boxes, an arrow towards the displayed top, and at
each displayed corner the user-space coordinates of that corner. manifest.json
records the same data for the tests.
"""

import argparse
import datetime
import hashlib
import json
import shutil
import subprocess
import sys
import tempfile
from dataclasses import dataclass
from pathlib import Path

HERE = Path(__file__).resolve().parent

A4 = (595.276, 841.89)
LETTER = (612.0, 792.0)

Box = tuple[float, float, float, float]

# PDFium, behind Qt PDF, shows an empty MediaBox as US Letter.
DEFAULT_MEDIA: Box = (0, 0, *LETTER)


def normalized(b: Box) -> Box:
    return (min(b[0], b[2]), min(b[1], b[3]), max(b[0], b[2]), max(b[1], b[3]))


def is_empty(b: Box) -> bool:
    return b[2] <= b[0] or b[3] <= b[1]


def pdfium_rotation(degrees: int) -> int:
    """/Rotate as PDFium reads it: truncated to a multiple of 90, then into [0, 360).
    Mirrors rotationFromDegrees in src/domain/page_geometry.cpp."""
    quarter_turns = int(degrees / 90) % 4
    return quarter_turns * 90


@dataclass(frozen=True)
class Page:
    """A page as written in the file. Attributes left to None are not written on
    the page: MediaBox, CropBox and /Rotate are then inherited from the page tree."""
    media: Box | None = None
    crop: Box | None = None
    rotate: int | None = None
    user_unit: float | None = None


@dataclass(frozen=True)
class Tree:
    """A page tree node (/Type /Pages), with the inheritable attributes it carries."""
    kids: tuple["Page | Tree", ...]
    media: Box | None = None
    crop: Box | None = None
    rotate: int | None = None


INHERITABLE = {"media": "MediaBox", "crop": "CropBox", "rotate": "Rotate"}


@dataclass(frozen=True)
class Geometry:
    """The effective geometry of a page, once inheritance is resolved. The boxes
    and /Rotate are the values as written; the visible area and the displayed
    orientation follow PDFium, like PageGeometry in the domain."""
    media: Box
    crop: Box | None
    rotate: int
    user_unit: float
    inherited: tuple[str, ...]  # PDF names of the attributes taken from the page tree

    @property
    def visible(self) -> Box | None:
        """The CropBox clipped to the MediaBox, None when nothing is visible."""
        media = normalized(self.media)
        if is_empty(media):
            media = DEFAULT_MEDIA
        if self.crop is None or is_empty(normalized(self.crop)):
            return media
        crop = normalized(self.crop)
        clipped = (max(media[0], crop[0]), max(media[1], crop[1]), min(media[2], crop[2]), min(media[3], crop[3]))
        return None if is_empty(clipped) else clipped

    @property
    def displayed_rotation(self) -> int:
        return pdfium_rotation(self.rotate)

    def displayed_size(self) -> tuple[float, float]:
        if self.visible is None:
            return (0, 0)
        x0, y0, x1, y1 = self.visible
        w, h = x1 - x0, y1 - y0
        return (h, w) if self.displayed_rotation in (90, 270) else (w, h)

    def displayed_to_user(self) -> tuple[float, float, float, float, float, float]:
        """Matrix mapping displayed coordinates (points, origin at the displayed
        bottom-left, y up) to user space, as a PDF `cm` operand list."""
        x0, y0, x1, y1 = self.visible
        w, h = x1 - x0, y1 - y0
        return {
            0: (1, 0, 0, 1, x0, y0),
            90: (0, 1, -1, 0, x0 + w, y0),
            180: (-1, 0, 0, -1, x0 + w, y0 + h),
            270: (0, -1, 1, 0, x0, y0 + h),
        }[self.displayed_rotation]

    def to_user(self, dx: float, dy: float) -> tuple[float, float]:
        a, b, c, d, e, f = self.displayed_to_user()
        return (a * dx + c * dy + e, b * dx + d * dy + f)

    def displayed_corners(self) -> dict[str, tuple[float, float]]:
        if self.visible is None:
            return {}
        dw, dh = self.displayed_size()
        corners = {"bottom_left": (0, 0), "bottom_right": (dw, 0), "top_left": (0, dh), "top_right": (dw, dh)}
        return {name: tuple(round(v, 3) for v in self.to_user(*pos)) for name, pos in corners.items()}


def resolve(page: Page, ancestors: list[Tree]) -> Geometry:
    """Apply page tree inheritance: the nearest node that sets an attribute wins."""
    values, inherited = {}, []
    for attribute, pdf_name in INHERITABLE.items():
        value = getattr(page, attribute)
        if value is None:
            value = next((getattr(n, attribute) for n in reversed(ancestors) if getattr(n, attribute) is not None), None)
            if value is not None:
                inherited.append(pdf_name)
        values[attribute] = value
    if values["media"] is None:
        raise ValueError("every page needs a MediaBox, on the page or in the page tree")
    return Geometry(
        media=values["media"], crop=values["crop"], rotate=values["rotate"] or 0,
        user_unit=page.user_unit or 1, inherited=tuple(inherited),
    )


def walk(tree: Tree, ancestors: tuple[Tree, ...] = ()):
    """Yield every page of the tree in document order, with its ancestors."""
    for kid in tree.kids:
        if isinstance(kid, Tree):
            yield from walk(kid, (*ancestors, tree))
        else:
            yield kid, [*ancestors, tree]


def pages_of(tree: Tree) -> list[Geometry]:
    return [resolve(page, ancestors) for page, ancestors in walk(tree)]


def portrait(size: tuple[float, float], **kwargs) -> Page:
    return Page(media=(0, 0, *size), **kwargs)


def landscape(size: tuple[float, float], **kwargs) -> Page:
    return Page(media=(0, 0, size[1], size[0]), **kwargs)


def flat(*pages: Page) -> Tree:
    return Tree(kids=pages)


# CropBox offsets: an A4 visible area inside a larger MediaBox, and a MediaBox
# whose origin is not (0, 0).
A4_CROPPED = dict(media=(0, 0, 700, 950), crop=(60, 90, 60 + A4[0], 90 + A4[1]))
LETTER_CROPPED_NEGATIVE = dict(media=(-50, -40, 700, 860), crop=(20, 30, 20 + LETTER[0], 30 + LETTER[1]))

GEOMETRY_FIXTURES: dict[str, Tree] = {
    "a4-portrait.pdf": flat(portrait(A4)),
    "a4-landscape.pdf": flat(landscape(A4)),
    "letter-portrait.pdf": flat(portrait(LETTER)),
    "letter-landscape.pdf": flat(landscape(LETTER)),
    "a4-rotate-90.pdf": flat(portrait(A4, rotate=90)),
    "a4-rotate-180.pdf": flat(portrait(A4, rotate=180)),
    "a4-rotate-270.pdf": flat(portrait(A4, rotate=270)),
    "a4-cropbox-offset.pdf": flat(Page(**A4_CROPPED)),
    "mixed.pdf": flat(
        portrait(A4),
        landscape(LETTER),
        portrait(A4, rotate=90),
        Page(**A4_CROPPED, rotate=180),
        Page(**LETTER_CROPPED_NEGATIVE, rotate=270),
    ),
    # Values PDFium reads as 270, 90, 90 and 0.
    "a4-rotate-unusual.pdf": flat(*(portrait(A4, rotate=r) for r in (-90, 450, 135, 45))),
    # MediaBox, CropBox and /Rotate set on page tree nodes, not on the pages.
    "inherited-attributes.pdf": Tree(
        media=(0, 0, *A4),
        rotate=90,
        kids=(
            Page(),
            Tree(
                **A4_CROPPED,
                rotate=180,
                kids=(Page(), Page(rotate=0)),
            ),
            landscape(LETTER),
        ),
    ),
    "degenerate-boxes.pdf": flat(
        Page(media=(0, 0, 0, 0), crop=A4_CROPPED["crop"]),
        Page(media=A4_CROPPED["media"], crop=(60, 90, 60, 90)),
        Page(media=A4_CROPPED["media"], crop=(800, 800, 900, 900)),
        Page(media=A4_CROPPED["media"], crop=(-20, 100, 800, 900)),
    ),
    # UserUnit 2: an A4 page in size, with a MediaBox half as large in user units.
    "a4-userunit-2.pdf": flat(Page(media=(0, 0, A4[0] / 2, A4[1] / 2), user_unit=2)),
}


def num(value: float) -> str:
    text = f"{value:.3f}".rstrip("0").rstrip(".")
    return "0" if text == "-0" else text


def box(values) -> str:
    return "[" + " ".join(num(v) for v in values) + "]"


def text_string(text: str) -> str:
    return "(" + text.replace("\\", "\\\\").replace("(", "\\(").replace(")", "\\)") + ")"


def text(x: float, y: float, size: int, content: str) -> str:
    return f"BT /F1 {size} Tf {num(x)} {num(y)} Td {text_string(content)} Tj ET\n"


def overlaps(a: Box, b: Box) -> bool:
    return a[0] < b[2] and b[0] < a[2] and a[1] < b[3] and b[1] < a[3]


def describe(page: Geometry) -> list[str]:
    def origin(pdf_name: str) -> str:
        return " (inherited)" if pdf_name in page.inherited else ""

    media = box(page.media) + (" (empty: shown as US Letter)" if is_empty(normalized(page.media)) else "")
    if page.crop is None:
        crop = "= MediaBox"
    elif is_empty(normalized(page.crop)):
        crop = box(page.crop) + " (empty: MediaBox shown)"
    else:
        crop = box(page.crop)
    rotate = f"{page.rotate}"
    if page.displayed_rotation != page.rotate:
        rotate += f" (read as {page.displayed_rotation})"
    lines = [
        f"MediaBox {media}{origin('MediaBox')}",
        f"CropBox {crop}{origin('CropBox')}",
        f"Rotate {rotate}{origin('Rotate')}",
    ]
    if page.user_unit != 1:
        lines.append(f"UserUnit {num(page.user_unit)}: 1 unit = {num(page.user_unit)}/72 inch")
    return lines


def page_content(name: str, index: int, count: int, page: Geometry) -> bytes:
    out = []
    visible = page.visible
    media = normalized(page.media)
    if is_empty(media):
        media = DEFAULT_MEDIA
    # Drawn in the MediaBox, outside the visible area: must never be visible.
    warning = (media[0] + 5, media[1] + 5, media[0] + 260, media[1] + 15)
    if visible != media and (visible is None or not overlaps(warning, visible)):
        out.append("0.8 0 0 rg\n" + text(warning[0], warning[1], 10, "OUTSIDE CROPBOX: MUST NOT BE VISIBLE"))
    if visible is None:
        return "".join(out).encode("ascii")

    out.append("q " + " ".join(num(v) for v in page.displayed_to_user()) + " cm\n")
    dw, dh = page.displayed_size()
    out.append(f"0 0 0.8 RG 2 w 5 5 {num(dw - 10)} {num(dh - 10)} re S\n")

    # Arrow towards the displayed top.
    cx = dw / 2
    out.append(f"{num(cx)} {num(dh - 120)} m {num(cx)} {num(dh - 30)} l S\n")
    out.append(f"0 0 0.8 rg {num(cx - 12)} {num(dh - 50)} m {num(cx)} {num(dh - 25)} l {num(cx + 12)} {num(dh - 50)} l f\n")
    out.append("0 g" + "\n" + text(cx + 18, dh - 60, 12, "TOP"))

    lines = [f"misign fixture: {name}", f"page {index + 1} of {count}", *describe(page)]
    for i, line in enumerate(lines):
        out.append(text(40, dh / 2 + 40 - i * 18, 14 if i == 0 else 11, line))

    # Each displayed corner, labelled with its user-space coordinates.
    for label, (ux, uy) in page.displayed_corners().items():
        tx = 12 if label.endswith("left") else dw - 150
        ty = 14 if label.startswith("bottom") else dh - 22
        out.append(text(tx, ty, 8, f"{label}: user ({num(ux)}, {num(uy)})"))

    out.append("Q\n")
    return "".join(out).encode("ascii")


def inheritable_entries(node: Page | Tree) -> list[str]:
    entries = []
    if node.media is not None:
        entries.append(f"/MediaBox {box(node.media)}")
    if node.crop is not None:
        entries.append(f"/CropBox {box(node.crop)}")
    if node.rotate is not None:
        entries.append(f"/Rotate {node.rotate}")
    return entries


def write_pdf(path: Path, name: str, tree: Tree) -> None:
    """Write a minimal PDF 1.7 file with a classic cross-reference table."""
    objects: list[bytes] = []

    def add(body: bytes) -> int:
        objects.append(body)
        return len(objects)

    catalog = add(b"")  # filled once the page tree number is known
    font = add(b"<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica /Encoding /WinAnsiEncoding >>")
    count = len(list(walk(tree)))
    index = 0

    def add_tree(node: Tree, parent: int | None, ancestors: list[Tree]) -> tuple[int, int]:
        """Add a /Pages node and its kids; return its object number and page count."""
        nonlocal index
        number = add(b"")  # filled once the kids are known
        kids, pages = [], 0
        for kid in node.kids:
            if isinstance(kid, Tree):
                kid_number, kid_pages = add_tree(kid, number, [*ancestors, node])
                kids.append(kid_number)
                pages += kid_pages
                continue
            geometry = resolve(kid, [*ancestors, node])
            content = page_content(name, index, count, geometry)
            index += 1
            stream = add(b"<< /Length %d >>\nstream\n" % len(content) + content + b"endstream")
            entries = [
                "/Type /Page",
                f"/Parent {number} 0 R",
                f"/MediaBox {box(kid.media)}" if kid.media is not None else "",
                f"/Resources << /Font << /F1 {font} 0 R >> >>",
                f"/Contents {stream} 0 R",
                *inheritable_entries(Page(crop=kid.crop, rotate=kid.rotate)),
            ]
            if kid.user_unit is not None:
                entries.append(f"/UserUnit {num(kid.user_unit)}")
            kids.append(add(("<< " + " ".join(e for e in entries if e) + " >>").encode("ascii")))
            pages += 1
        entries = ["/Type /Pages"]
        if parent is not None:
            entries.append(f"/Parent {parent} 0 R")
        entries += [
            "/Kids [" + " ".join(f"{k} 0 R" for k in kids) + "]",
            f"/Count {pages}",
            *inheritable_entries(node),
        ]
        objects[number - 1] = ("<< " + " ".join(entries) + " >>").encode("ascii")
        return number, pages

    root, _ = add_tree(tree, None, [])
    objects[catalog - 1] = f"<< /Type /Catalog /Pages {root} 0 R >>".encode("ascii")

    data = bytearray(b"%PDF-1.7\n%\xe2\xe3\xcf\xd3\n")
    offsets = []
    for number, body in enumerate(objects, start=1):
        offsets.append(len(data))
        data += b"%d 0 obj\n" % number + body + b"\nendobj\n"
    xref = len(data)
    data += b"xref\n0 %d\n0000000000 65535 f \n" % (len(objects) + 1)
    for offset in offsets:
        data += b"%010d 00000 n \n" % offset
    file_id = hashlib.md5(name.encode("utf-8"), usedforsecurity=False).hexdigest()
    data += (
        f"trailer\n<< /Size {len(objects) + 1} /Root {catalog} 0 R /ID [<{file_id}> <{file_id}>] >>\n"
        f"startxref\n{xref}\n%%EOF\n"
    ).encode("ascii")
    path.write_bytes(bytes(data))


def make_test_certificate(directory: Path) -> tuple[Path, Path]:
    """Create a throwaway self-signed certificate. Only the certificate is kept
    in the corpus, so tests can validate the signature against it."""
    from cryptography import x509
    from cryptography.hazmat.primitives import hashes, serialization
    from cryptography.hazmat.primitives.asymmetric import rsa
    from cryptography.x509.oid import NameOID

    key = rsa.generate_private_key(public_exponent=65537, key_size=2048)
    subject = x509.Name([
        x509.NameAttribute(NameOID.COMMON_NAME, "Misign test fixture"),
        x509.NameAttribute(NameOID.ORGANIZATION_NAME, "Misign"),
    ])
    certificate = (
        x509.CertificateBuilder()
        .subject_name(subject)
        .issuer_name(subject)
        .public_key(key.public_key())
        .serial_number(x509.random_serial_number())
        .not_valid_before(datetime.datetime(2026, 1, 1, tzinfo=datetime.timezone.utc))
        .not_valid_after(datetime.datetime(2126, 1, 1, tzinfo=datetime.timezone.utc))
        .add_extension(x509.BasicConstraints(ca=True, path_length=None), critical=True)
        .add_extension(
            x509.KeyUsage(
                digital_signature=True, content_commitment=True, key_encipherment=False,
                data_encipherment=False, key_agreement=False, key_cert_sign=True,
                crl_sign=False, encipher_only=False, decipher_only=False,
            ),
            critical=True,
        )
        .sign(key, hashes.SHA256())
    )
    key_path = directory / "key.pem"
    cert_path = directory / "cert.pem"
    key_path.write_bytes(key.private_bytes(
        serialization.Encoding.PEM, serialization.PrivateFormat.PKCS8, serialization.NoEncryption()))
    cert_path.write_bytes(certificate.public_bytes(serialization.Encoding.PEM))
    return key_path, cert_path


def write_signed(source: Path, target: Path, cert_target: Path, certify: int | None = None) -> None:
    """Sign `source` with a visible signature, as an incremental update: an
    approval signature, or with `certify` a certification signature whose DocMDP
    permission level P is `certify` (1: no changes, 2: form filling and signing,
    3: also annotations). A new throwaway key is made on every call."""
    from pyhanko.keys import load_cert_from_pemder
    from pyhanko.pdf_utils.incremental_writer import IncrementalPdfFileWriter
    from pyhanko.pdf_utils.reader import PdfFileReader
    from pyhanko.sign import fields, signers
    from pyhanko.sign.validation import read_certification_data, validate_pdf_signature
    from pyhanko_certvalidator import ValidationContext

    with tempfile.TemporaryDirectory() as tmp:
        key_path, cert_path = make_test_certificate(Path(tmp))
        signer = signers.SimpleSigner.load(str(key_path), str(cert_path))
        with source.open("rb") as inf, target.open("wb") as outf:
            writer = IncrementalPdfFileWriter(inf)
            fields.append_signature_field(writer, fields.SigFieldSpec("Signature1", box=(60, 60, 260, 120)))
            metadata = signers.PdfSignatureMetadata(
                field_name="Signature1", reason="Misign test fixture", subfilter=fields.SigSeedSubFilter.PADES,
                certify=certify is not None,
                docmdp_permissions=fields.MDPPerm(certify or fields.MDPPerm.FILL_FORMS),
            )
            signers.sign_pdf(writer, metadata, signer=signer, output=outf)
        shutil.copyfile(cert_path, cert_target)

    root = load_cert_from_pemder(str(cert_target))
    with target.open("rb") as inf:
        reader = PdfFileReader(inf)
        status = validate_pdf_signature(reader.embedded_signatures[0], ValidationContext(trust_roots=[root]))
        certification = read_certification_data(reader)
    if not (status.intact and status.valid and status.trusted):
        sys.exit(f"{target.name}: the signature does not validate: {status.summary()}")
    level = certification.permission.value if certification else None
    if level != certify:
        sys.exit(f"{target.name}: DocMDP level {level}, expected {certify}")


# PDF/A requires an output intent: Ghostscript takes it from a PostScript prologue
# (a trimmed-down copy of its lib/PDFA_def.ps) pointing to an sRGB ICC profile.
PDFA_PROLOGUE = """\
[ /Title (misign fixture: pdfa-2b-a4.pdf) /DOCINFO pdfmark
[ /_objdef {icc_PDFA} /type /stream /OBJ pdfmark
[ {icc_PDFA} << /N 3 >> /PUT pdfmark
[ {icc_PDFA} (%s) (r) file /PUT pdfmark
[ /_objdef {OutputIntent_PDFA} /type /dict /OBJ pdfmark
[ {OutputIntent_PDFA} <<
  /Type /OutputIntent /S /GTS_PDFA1 /DestOutputProfile {icc_PDFA} /OutputConditionIdentifier (sRGB)
>> /PUT pdfmark
[ {Catalog} << /OutputIntents [ {OutputIntent_PDFA} ] >> /PUT pdfmark
"""

SRGB_PROFILES = ["/usr/share/color/icc/ghostscript/srgb.icc", "/usr/share/color/icc/sRGB.icc"]


def write_pdfa(source: Path, target: Path) -> None:
    """Convert `source` to PDF/A-2b with Ghostscript, which embeds the fonts."""
    profile = next((p for p in SRGB_PROFILES if Path(p).is_file()), None)
    if profile is None:
        sys.exit(f"no sRGB ICC profile found (looked for {', '.join(SRGB_PROFILES)})")
    prologue = source.with_name("PDFA_def.ps")
    prologue.write_text(PDFA_PROLOGUE % profile)
    subprocess.run(
        [
            "gs", "-q", "-dBATCH", "-dNOPAUSE", "-dSAFER", f"--permit-file-read={profile}",
            "-sDEVICE=pdfwrite", "-dPDFA=2", "-dPDFACompatibilityPolicy=1", "-sColorConversionStrategy=RGB",
            f"-sOutputFile={target}", str(prologue), str(source),
        ],
        check=True,
    )


def rounded(values) -> list[float] | None:
    return None if values is None else [round(v, 3) for v in values]


def manifest_entry(tree: Tree) -> list[dict]:
    return [
        {
            "media_box": rounded(page.media),
            "crop_box": rounded(page.crop),
            "rotate": page.rotate,
            "user_unit": page.user_unit,
            "inherited": list(page.inherited),
            "visible_box": rounded(page.visible),
            "displayed_size": rounded(page.displayed_size()),
            "displayed_corners_in_user_space": page.displayed_corners(),
        }
        for page in pages_of(tree)
    ]


def signed(cert_name: str, certify: int | None = None):
    return lambda src, dst, out_dir: write_signed(src, dst, out_dir / cert_name, certify)


# A4 portrait pages turned into other files by external tools: (file, needs
# pyHanko, producer). Each certified file has its own certificate.
DERIVED_FIXTURES = [
    ("signed-a4.pdf", True, signed("signed-a4.cert.pem")),
    *((f"certified-p{p}-a4.pdf", True, signed(f"certified-p{p}-a4.cert.pem", certify=p)) for p in (1, 2, 3)),
    ("pdfa-2b-a4.pdf", False, lambda src, dst, _: write_pdfa(src, dst)),
]


def generate(out_dir: Path, skip_signed: bool, skip_pdfa: bool) -> None:
    manifest = {}
    for name, tree in GEOMETRY_FIXTURES.items():
        write_pdf(out_dir / name, name, tree)
        manifest[name] = manifest_entry(tree)

    base = flat(portrait(A4))
    for name, needs_pyhanko, produce in DERIVED_FIXTURES:
        manifest[name] = manifest_entry(base)
        if skip_signed if needs_pyhanko else skip_pdfa:
            continue
        with tempfile.TemporaryDirectory() as tmp:
            source = Path(tmp) / "source.pdf"
            write_pdf(source, name, base)
            produce(source, out_dir / name, out_dir)

    (out_dir / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")


def check() -> None:
    """Regenerate the deterministic files and compare them with the committed ones."""
    with tempfile.TemporaryDirectory() as tmp:
        generate(Path(tmp), skip_signed=True, skip_pdfa=True)
        drifted = sorted(
            path.name for path in Path(tmp).iterdir()
            if not (HERE / path.name).is_file() or (HERE / path.name).read_bytes() != path.read_bytes()
        )
    if drifted:
        sys.exit(f"out of date, run generate.py again: {', '.join(drifted)}")
    print("the committed fixtures match generate.py")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--skip-signed", action="store_true", help="do not regenerate the signed and certified files (need pyHanko)")
    parser.add_argument("--skip-pdfa", action="store_true", help="do not regenerate pdfa-2b-a4.pdf (needs Ghostscript)")
    parser.add_argument("--check", action="store_true",
                        help="write nothing, fail if the deterministic files differ from generate.py's output")
    args = parser.parse_args()

    if args.check:
        check()
    else:
        generate(HERE, args.skip_signed, args.skip_pdfa)


if __name__ == "__main__":
    main()
