# 0005 — PDF libraries: Qt PDF and PoDoFo

- **Status**: accepted; writing validated by the PHY-82 prototype, placement pending PHY-83
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
- **qpdf** remains a test tool (`qpdf --check`) and the fallback adapter if PoDoFo proves unsuitable.

The prototype (`spike`) must verify that:

1. a digitally signed PDF keeps a valid signature after the drawn signature is added;
2. rotated pages (`/Rotate`) and offset `CropBox` values produce an exact placement.

## Prototype results

**Point 1 (PHY-82, [spike](../../spikes/phy-82-podofo-incremental/README.md)): validated.** On the signed fixture, `SaveUpdate` keeps the original bytes and appends one revision; `qpdf --check`, `pdfsig` and pyHanko all report the existing signature as intact and valid, whether the drawing goes into the page content or into an annotation. Three findings shape the decision above:

- by default PoDoFo's garbage collection frees objects in the incremental update, which signature validators flag as a suspicious modification: `NoCollectGarbage` prevents it;
- readers accept annotations added after a signature, but report page content changes; DocMDP also permits annotations (level 3), never content changes: the drawing goes into an annotation;
- Qt PDF only draws annotations with `RenderFlag::Annotations`, and never draws form field widgets, so an existing visible digital signature appears blank.

Not covered yet: a check in Adobe Acrobat Reader, and certified documents (`/Perms /DocMDP`), where `P < 3` forbids even annotations.

**Point 2 (PHY-83)**: pending.

## Consequences

- Ubuntu 24.04 only ships PoDoFo 0.9.8: the dev container builds PoDoFo from source. CI will have to do the same, or use vcpkg, especially on Windows.
- Qt PDF is installed with Qt by `scripts/install_qt.py` (see ADR 0006).
- PDF/A validation with veraPDF is postponed to a later release.
- Misign must detect certified documents and refuse, or warn, when their DocMDP permissions (`P < 3`) do not allow annotations.
- An annotation can be removed by any PDF editor, as a content stream could be rewritten by one: the drawn signature is not a tamper-proof seal, only a digital signature is.
