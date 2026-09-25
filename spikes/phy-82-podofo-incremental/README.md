# PHY-82 spike: PoDoFo incremental update on a signed PDF

Throwaway code answering one question before building on [ADR 0005](../../docs/adr/0005-pdf-libraries.md): can PoDoFo 1.1 add a vector drawing to a page of an already signed PDF, as an incremental update, without invalidating the existing signature? It is not part of the Misign build and is not maintained.

**Answer: yes**, provided the drawing is added as an annotation and PoDoFo's garbage collection is disabled on save. Details below.

## What it does

- `draw.cpp` (PoDoFo 1.1.2) opens a copy of `tests/fixtures/pdf/signed-a4.pdf`, draws a signature-like stroke made of cubic Bézier curves with `PdfPainter`, and saves with `PdfMemDocument::SaveUpdate` on the same file. Two ways of adding the drawing:
  - `content`: `PdfPainter` on the page itself, which appends a content stream to the page;
  - `annotation`: `PdfPainter` on a form XObject, used as the normal appearance (`/AP /N`) of a `/Stamp` annotation (flags `Print | Locked`).

  Each variant is tried with the `NoMetadataUpdate` and `NoCollectGarbage` save options on and off.
- `render.cpp` (Qt PDF 6.11.3) renders page 1 with and without `QPdfDocumentRenderOptions::RenderFlag::Annotations` and counts the blue pixels of the drawing.
- `check.py` runs every variant on a fresh copy and checks the result with `qpdf --check`, `pdfsig` (poppler 24.02), pyHanko 0.37 signature validation, and the Qt PDF render. It also prints the free entries of the new revision's cross-reference table and the objects it defines (top-level `N G obj` definitions only: it warns if the revision uses an object stream, whose objects it would miss).

## Running it

In the dev container, with `poppler-utils` installed (`sudo apt-get install poppler-utils`) and the pyHanko environment of `tests/fixtures/pdf/README.md`:

```sh
cmake -S spikes/phy-82-podofo-incremental -B build-container/spike-82 -G Ninja
cmake --build build-container/spike-82
QT_QPA_PLATFORM=offscreen ~/.venvs/fixtures/bin/python spikes/phy-82-podofo-incremental/check.py build-container/spike-82
```

The PDFs and PNG renders are written to `build-container/spike-82/out/`.

## Results

For every variant:

- The original bytes are kept unchanged: the output starts with the exact bytes of `signed-a4.pdf`, followed by one new revision (3 revisions in total).
- `qpdf --check` passes.
- `pdfsig`: "Signature is Valid".
- pyHanko: the signature is intact, valid and trusted, and covers its entire revision.

| Variant | New revision contents | Freed objects | Qt PDF, no annotation flag | Qt PDF, `Annotations` flag |
|---|---|---|---|---|
| `content` | page dict, 2 content streams, `/Contents` array, `/Info` | 4, 6, 9 (4 is the original content stream) | drawing visible | drawing visible |
| `content` + `NoCollectGarbage` | page dict, content streams, `/Contents` array, `/Info` | none | visible | visible |
| `annotation` | page dict (`/Annots` += 1), form XObject, `/Stamp` annotation, `/Info` | 6, 9 | **not visible** | visible |
| `annotation` + `NoMetadataUpdate` + `NoCollectGarbage` | page dict, form XObject, `/Stamp` annotation | none | **not visible** | visible |

The table shows 4 of the 8 variants. The others only combine the two options: `NoMetadataUpdate` alone removes the `/Info` dictionary from the new revision (the fixture has no XMP metadata stream), and `NoCollectGarbage` alone removes the freed objects; neither changes anything else.

### Findings

1. **The cryptographic signature always survives.** `SaveUpdate` appends a revision and never rewrites the signed byte range, in every variant.
2. **By default, PoDoFo frees objects in the incremental update.** Its garbage collection frees objects that are no longer referenced, including the original page content stream in the `content` variant. A validator doing modification analysis reports this as suspicious (pyHanko: *"refs … were freed in the revision provided"*). `PdfSaveOptions::NoCollectGarbage` avoids it.
3. **`NoMetadataUpdate` keeps the revision minimal.** Without it, PoDoFo rewrites the `/Info` dictionary (`ModDate`) and the XMP metadata. Metadata changes are generally tolerated, but there is no reason to touch them.
4. **Content streams versus annotation: decided from the specification, not observed.** Both keep the signature cryptographically valid, and none of the oracles used here tells them apart: `pdfsig` does no modification analysis, pyHanko classifies both as `OTHER` (finding 5), and Acrobat was not run. The choice rests on the PDF specification: annotations are changes that may be added after signing, and DocMDP lists them as a permitted change (level 3), never page content, while page content changes are what readers report against a signature (see [Shadow Attacks, NDSS 2021](https://www.ndss-symposium.org/wp-content/uploads/ndss2021_1B-4_24117_paper.pdf), section II, and [pdf-insecurity.org](https://www.pdf-insecurity.org/)). The annotation variant is the right one; a check in Acrobat Reader is the test that can confirm it.
5. **pyHanko cannot tell the two variants apart.** Its default modification policy does not implement the "annotations" level: any annotation added outside form filling is classified as `OTHER` (see `ModificationLevel.ANNOTATIONS` in pyHanko). It is a good oracle for the cryptographic check, not for modification analysis.
6. **Qt PDF only renders annotations on request.** The renderer must pass `RenderFlag::Annotations`, or the drawn signature is invisible in Misign.
7. **Qt PDF does not render form field widgets.** The visible signature box that pyHanko put on `signed-a4.pdf` is drawn by Ghostscript but not by Qt PDF, with or without the annotation flag. Misign would show an existing visible digital signature as blank. The likely cause is PDFium: page rendering with `FPDF_ANNOT` skips widget annotations, which are only drawn through the form-fill API (`FPDF_FFLDraw`), and `QPdfDocument` does not expose it. No render option will fix it: Misign would have to read the widget rectangles itself (e.g. with PoDoFo) and draw them over the page.

### Not covered

- **Adobe Acrobat Reader** was not run: a manual check on Windows is still needed (open the `annotation` output and look at the signature panel).
- **Certified documents (DocMDP).** With `P=1` no change is allowed at all, with `P=2` only form filling and signing, so an annotation still breaks the certification. The corpus has no certified PDF yet.
- **Other signature types**: a PAdES signature with a timestamp or long-term validation data (DSS), and several signatures in one file.

## Recommendation

Accept PoDoFo (ADR 0005), with these rules for the PoDoFo adapter:

- add the handwritten signature as a `/Stamp` annotation whose appearance is a form XObject, never into the page content;
- save with `SaveUpdate(…, PdfSaveOptions::NoCollectGarbage | PdfSaveOptions::NoMetadataUpdate)`;
- render pages with `RenderFlag::Annotations` in the Qt PDF adapter.

Follow-ups:

- detect certified documents (`/Perms /DocMDP`) and refuse or warn when `P < 3`, with certified fixtures in the corpus;
- handle form field widgets not being rendered by Qt PDF: read their rectangles with PoDoFo and at least show where existing visible signatures are;
- check the `annotation` output in Acrobat Reader.
