# 0005 — PDF libraries: Qt PDF and PoDoFo

- **Status**: accepted; validated by the PHY-82 and PHY-83 prototypes
- **Date**: 2026-09-25

## Context

Two separate needs:

- **Rendering** pages (`IPdfRenderer`).
- **Writing** the signature into a copy of the document (`IPdfWriter`).

The reference corpus contains PDFs that already carry digital signatures. Fully rewriting such a file invalidates its signature. Only an **incremental update** (appending changes at the end of the file) preserves it.

Dependencies must be GPLv3-compatible (see 0003).

## Options considered

| | PoDoFo (LGPL) | qpdf (Apache 2.0) | MuPDF |
|---|---|---|---|
| Drawing the signature | Drawing API (`PdfPainter`) | Hand-written content stream | — |
| Incremental update | Yes (`SaveUpdate`, since 0.10) | No, full rewrite | — |
| Robustness with malformed PDFs | Fair | Excellent | — |
| License | Compatible | Compatible | AGPL, excluded |

## Decision

- **Rendering**: Qt PDF (`QPdfDocument`, based on PDFium), part of Qt 6.
- **Writing**: PoDoFo 1.1 or later, using incremental updates. The signed copy is produced by copying the source file, then calling `SaveUpdate` on the copy, with `PdfSaveOptions::NoCollectGarbage | PdfSaveOptions::NoMetadataUpdate`. The drawn signature is added as a `/Stamp` annotation whose normal appearance is a form XObject, never into the page content.
- **Rendering flags**: the Qt PDF adapter renders with `QPdfDocumentRenderOptions::RenderFlag::Annotations`, otherwise the drawn signature is not displayed.
- **Placement**: screen → PDF conversion is a domain rule (`src/domain/page_geometry.h`). The PoDoFo adapter reads page boxes and rotation with the raw getters (`GetMediaBoxRaw`, `GetCropBoxRaw`, `TryGetRotationRaw`) and places the annotation with `SetRectRaw` and `SetAppearanceStreamRaw`; the form XObject is drawn in displayed orientation and carries the rotation part of `PageGeometry::displayedToUser()` as its `/Matrix`. PoDoFo's non-raw APIs adjust coordinates for the page rotation and must not be used.
- **qpdf** remains a test tool (`qpdf --check`) and the fallback adapter if PoDoFo proves unsuitable.

The prototype (`spike`) must verify that:

1. a digitally signed PDF keeps a valid signature after the drawn signature is added;
2. rotated pages (`/Rotate`) and offset `CropBox` values produce an exact placement.

## Prototype results

**Point 1 (PHY-82, [spike](../../spikes/phy-82-podofo-incremental/README.md)): validated.** On the signed fixture, `SaveUpdate` keeps the original bytes and appends one revision; `qpdf --check`, `pdfsig` and pyHanko all report the existing signature as intact and valid, whether the drawing goes into the page content or into an annotation. Three findings shape the decision above:

- observed: by default PoDoFo's garbage collection frees objects in the incremental update, which signature validators flag as a suspicious modification; `NoCollectGarbage` prevents it;
- per the PDF specification, not observed (no available oracle tells the two variants apart): annotations may be added after a signature, and DocMDP permits them (level 3), while page content changes are what readers report against a signature. The drawing therefore goes into an annotation;
- observed: Qt PDF only draws annotations with `RenderFlag::Annotations`, and never draws form field widgets, so an existing visible digital signature appears blank. The likely cause is PDFium, which only draws widgets through its form-fill API, not exposed by `QPdfDocument`.

Not covered yet: a check in Adobe Acrobat Reader, the only test that can confirm the annotation choice in a reader, and certified documents (`/Perms /DocMDP`), where `P < 3` forbids even annotations.

**Point 2 (PHY-83, [spike](../../spikes/phy-83-placement/README.md)): validated.** All observed: on the 15 pages of the corpus (A4 and Letter, portrait and landscape, `/Rotate` 0/90/180/270, offset CropBox, negative MediaBox origin) at 4 zoom levels, a rectangle selected on the page as displayed by Qt PDF, converted with `PageGeometry` and drawn with PoDoFo's raw APIs, is rendered by Qt PDF within 0.8 pixel of the selection and upright (60 of 60). Qt PDF's page size always matches the domain's displayed size, and unusual `/Rotate` values (-90, 450, 135, 45) are displayed the way `rotationFromDegrees` reads them. Degenerate boxes (empty MediaBox or CropBox, CropBox outside the MediaBox) are displayed as PDFium computes them, which `PageGeometry` follows. On the signed fixture, placement combined with the PHY-82 save options keeps the existing signature valid. The same rectangle fed to PoDoFo's convenience API is misplaced on every rotated page (24 of 24). Added to the corpus later (PHY-87), observed with Qt PDF only: pages with `/Rotate`, `MediaBox` and `CropBox` inherited from the page tree are displayed upright at the expected size, so Qt PDF follows inheritance; `UserUnit` is ignored by Qt PDF (page size in user units, not scaled). Not covered yet: whether PoDoFo's raw getters follow inheritance, to be checked by the adapter's integration tests.

## Consequences

- Ubuntu 24.04 only ships PoDoFo 0.9.8: the dev container builds PoDoFo from source. CI will have to do the same, or use vcpkg, especially on Windows.
- Qt PDF is installed with Qt by `scripts/install_qt.py` (see ADR 0006).
- PDF/A validation with veraPDF is postponed to a later release.
- Misign must detect certified documents and refuse, or warn, when their DocMDP permissions (`P < 3`) do not allow annotations.
- An annotation can be removed by any PDF editor, as a content stream could be rewritten by one: the drawn signature is not a tamper-proof seal, only a digital signature is.
