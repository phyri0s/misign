#!/usr/bin/env python3
"""Generate the reference PDF corpus used by the tests.

Every file in this directory is produced by this script, so the corpus can be
redistributed under the project license. See README.md for what each file covers.

The geometry fixtures are written by hand with the standard library only: their
bytes are deterministic. Two files need external tools, and change on every run:

- signed-a4.pdf is signed with pyHanko (`pip install -r requirements.txt`);
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


@dataclass(frozen=True)
class Page:
    media: tuple[float, float, float, float]
    crop: tuple[float, float, float, float] | None = None
    rotate: int = 0

    @property
    def visible(self) -> tuple[float, float, float, float]:
        return self.crop or self.media

    def displayed_size(self) -> tuple[float, float]:
        x0, y0, x1, y1 = self.visible
        w, h = x1 - x0, y1 - y0
        return (h, w) if self.rotate in (90, 270) else (w, h)

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
        }[self.rotate]

    def to_user(self, dx: float, dy: float) -> tuple[float, float]:
        a, b, c, d, e, f = self.displayed_to_user()
        return (a * dx + c * dy + e, b * dx + d * dy + f)

    def displayed_corners(self) -> dict[str, tuple[float, float]]:
        dw, dh = self.displayed_size()
        corners = {"bottom_left": (0, 0), "bottom_right": (dw, 0), "top_left": (0, dh), "top_right": (dw, dh)}
        return {name: tuple(round(v, 3) for v in self.to_user(*pos)) for name, pos in corners.items()}


def portrait(size: tuple[float, float], **kwargs) -> Page:
    return Page(media=(0, 0, *size), **kwargs)


def landscape(size: tuple[float, float], **kwargs) -> Page:
    return Page(media=(0, 0, size[1], size[0]), **kwargs)


# CropBox offsets: an A4 visible area inside a larger MediaBox, and a MediaBox
# whose origin is not (0, 0).
A4_CROPPED = dict(media=(0, 0, 700, 950), crop=(60, 90, 60 + A4[0], 90 + A4[1]))
LETTER_CROPPED_NEGATIVE = dict(media=(-50, -40, 700, 860), crop=(20, 30, 20 + LETTER[0], 30 + LETTER[1]))

GEOMETRY_FIXTURES: dict[str, list[Page]] = {
    "a4-portrait.pdf": [portrait(A4)],
    "a4-landscape.pdf": [landscape(A4)],
    "letter-portrait.pdf": [portrait(LETTER)],
    "letter-landscape.pdf": [landscape(LETTER)],
    "a4-rotate-90.pdf": [portrait(A4, rotate=90)],
    "a4-rotate-180.pdf": [portrait(A4, rotate=180)],
    "a4-rotate-270.pdf": [portrait(A4, rotate=270)],
    "a4-cropbox-offset.pdf": [Page(**A4_CROPPED)],
    "mixed.pdf": [
        portrait(A4),
        landscape(LETTER),
        portrait(A4, rotate=90),
        Page(**A4_CROPPED, rotate=180),
        Page(**LETTER_CROPPED_NEGATIVE, rotate=270),
    ],
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


def page_content(name: str, index: int, count: int, page: Page) -> bytes:
    out = []
    if page.crop:
        # Drawn in the MediaBox, outside the CropBox: must never be visible.
        mx0, my0, _, _ = page.media
        out.append("0.8 0 0 rg\n" + text(mx0 + 5, my0 + 5, 10, "OUTSIDE CROPBOX: MUST NOT BE VISIBLE"))

    out.append("q " + " ".join(num(v) for v in page.displayed_to_user()) + " cm\n")
    dw, dh = page.displayed_size()
    out.append(f"0 0 0.8 RG 2 w 5 5 {num(dw - 10)} {num(dh - 10)} re S\n")

    # Arrow towards the displayed top.
    cx = dw / 2
    out.append(f"{num(cx)} {num(dh - 120)} m {num(cx)} {num(dh - 30)} l S\n")
    out.append(f"0 0 0.8 rg {num(cx - 12)} {num(dh - 50)} m {num(cx)} {num(dh - 25)} l {num(cx + 12)} {num(dh - 50)} l f\n")
    out.append("0 g" + "\n" + text(cx + 18, dh - 60, 12, "TOP"))

    lines = [
        f"misign fixture: {name}",
        f"page {index + 1} of {count}",
        f"MediaBox {box(page.media)}",
        f"CropBox {box(page.crop) if page.crop else '= MediaBox'}",
        f"Rotate {page.rotate}",
    ]
    for i, line in enumerate(lines):
        out.append(text(40, dh / 2 + 40 - i * 18, 14 if i == 0 else 11, line))

    # Each displayed corner, labelled with its user-space coordinates.
    for label, (ux, uy) in page.displayed_corners().items():
        tx = 12 if label.endswith("left") else dw - 150
        ty = 14 if label.startswith("bottom") else dh - 22
        out.append(text(tx, ty, 8, f"{label}: user ({num(ux)}, {num(uy)})"))

    out.append("Q\n")
    return "".join(out).encode("ascii")


def write_pdf(path: Path, name: str, pages: list[Page]) -> None:
    """Write a minimal PDF 1.7 file with a classic cross-reference table."""
    objects: list[bytes] = []

    def add(body: bytes) -> int:
        objects.append(body)
        return len(objects)

    catalog = add(b"")  # filled once the page tree number is known
    font = add(b"<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica /Encoding /WinAnsiEncoding >>")
    tree = add(b"")
    kids = []
    for index, page in enumerate(pages):
        content = page_content(name, index, len(pages), page)
        stream = add(b"<< /Length %d >>\nstream\n" % len(content) + content + b"endstream")
        entries = [
            "/Type /Page",
            f"/Parent {tree} 0 R",
            f"/MediaBox {box(page.media)}",
            f"/Resources << /Font << /F1 {font} 0 R >> >>",
            f"/Contents {stream} 0 R",
        ]
        if page.crop:
            entries.append(f"/CropBox {box(page.crop)}")
        if page.rotate:
            entries.append(f"/Rotate {page.rotate}")
        kids.append(add(("<< " + " ".join(entries) + " >>").encode("ascii")))

    objects[catalog - 1] = f"<< /Type /Catalog /Pages {tree} 0 R >>".encode("ascii")
    refs = " ".join(f"{k} 0 R" for k in kids)
    objects[tree - 1] = f"<< /Type /Pages /Kids [{refs}] /Count {len(kids)} >>".encode("ascii")

    data = bytearray(b"%PDF-1.7\n%\xe2\xe3\xcf\xd3\n")
    offsets = []
    for number, body in enumerate(objects, start=1):
        offsets.append(len(data))
        data += b"%d 0 obj\n" % number + body + b"\nendobj\n"
    xref = len(data)
    data += b"xref\n0 %d\n0000000000 65535 f \n" % (len(objects) + 1)
    for offset in offsets:
        data += b"%010d 00000 n \n" % offset
    file_id = hashlib.md5(name.encode("utf-8")).hexdigest()
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


def write_signed(source: Path, target: Path, cert_target: Path) -> None:
    """Sign `source` with a visible approval signature, as an incremental update."""
    from pyhanko.keys import load_cert_from_pemder
    from pyhanko.pdf_utils.incremental_writer import IncrementalPdfFileWriter
    from pyhanko.pdf_utils.reader import PdfFileReader
    from pyhanko.sign import fields, signers
    from pyhanko.sign.validation import validate_pdf_signature
    from pyhanko_certvalidator import ValidationContext

    with tempfile.TemporaryDirectory() as tmp:
        key_path, cert_path = make_test_certificate(Path(tmp))
        signer = signers.SimpleSigner.load(str(key_path), str(cert_path))
        with source.open("rb") as inf, target.open("wb") as outf:
            writer = IncrementalPdfFileWriter(inf)
            fields.append_signature_field(writer, fields.SigFieldSpec("Signature1", box=(60, 60, 260, 120)))
            metadata = signers.PdfSignatureMetadata(
                field_name="Signature1", reason="Misign test fixture", subfilter=fields.SigSeedSubFilter.PADES)
            signers.sign_pdf(writer, metadata, signer=signer, output=outf)
        shutil.copyfile(cert_path, cert_target)

    root = load_cert_from_pemder(str(cert_target))
    with target.open("rb") as inf:
        signature = PdfFileReader(inf).embedded_signatures[0]
        status = validate_pdf_signature(signature, ValidationContext(trust_roots=[root]))
    if not (status.intact and status.valid and status.trusted):
        sys.exit(f"{target.name}: the signature does not validate: {status.summary()}")


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


def manifest_entry(pages: list[Page]) -> list[dict]:
    return [
        {
            "media_box": [round(v, 3) for v in page.media],
            "crop_box": [round(v, 3) for v in page.visible],
            "rotate": page.rotate,
            "displayed_size": [round(v, 3) for v in page.displayed_size()],
            "displayed_corners_in_user_space": page.displayed_corners(),
        }
        for page in pages
    ]


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--skip-signed", action="store_true", help="do not regenerate signed-a4.pdf (needs pyHanko)")
    parser.add_argument("--skip-pdfa", action="store_true", help="do not regenerate pdfa-2b-a4.pdf (needs Ghostscript)")
    args = parser.parse_args()

    manifest = {}
    for name, pages in GEOMETRY_FIXTURES.items():
        write_pdf(HERE / name, name, pages)
        manifest[name] = manifest_entry(pages)

    base_pages = [portrait(A4)]
    for name, skip, produce in [
        ("signed-a4.pdf", args.skip_signed, lambda src, dst: write_signed(src, dst, HERE / "signed-a4.cert.pem")),
        ("pdfa-2b-a4.pdf", args.skip_pdfa, write_pdfa),
    ]:
        manifest[name] = manifest_entry(base_pages)
        if skip:
            continue
        with tempfile.TemporaryDirectory() as tmp:
            source = Path(tmp) / "source.pdf"
            write_pdf(source, name, base_pages)
            produce(source, HERE / name)

    (HERE / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")


if __name__ == "__main__":
    main()
