# 0005 — PDF libraries: Qt PDF and PoDoFo

- **Status**: accepted, pending prototype validation
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
- **Writing**: PoDoFo 1.1 or later, using incremental updates. The signed copy is produced by copying the source file, then calling `SaveUpdate` on the copy.
- **qpdf** remains a test tool (`qpdf --check`) and the fallback adapter if PoDoFo proves unsuitable.

The prototype (`spike`) must verify that:

1. a digitally signed PDF keeps a valid signature after the drawn signature is added;
2. rotated pages (`/Rotate`) and offset `CropBox` values produce an exact placement.

## Consequences

- Ubuntu 24.04 only ships PoDoFo 0.9.8: the dev container builds PoDoFo from source. CI will have to do the same, or use vcpkg, especially on Windows.
- Qt PDF is installed with Qt by `scripts/install_qt.py` (see ADR 0006).
- PDF/A validation with veraPDF is postponed to a later release.
