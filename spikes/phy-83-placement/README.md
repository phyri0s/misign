# PHY-83 spike: exact signature placement

Throwaway harness answering one question before the Signature box milestone: given a rectangle selected on a page as Qt PDF displays it, can we compute the matching rectangle in PDF user space for every page of the corpus? It is not part of the Misign build and is not maintained. The conversion itself is not throwaway: it lives in [`src/domain/page_geometry.h`](../../src/domain/page_geometry.h), with unit tests in [`tests/unit/tst_page_geometry.cpp`](../../tests/unit/tst_page_geometry.cpp).

**Answer: yes**, to within 0.8 pixel on every page of the corpus at every zoom level tested, provided the PoDoFo adapter uses PoDoFo's raw APIs. Details below.

## The conversion (`src/domain/page_geometry.h`)

`PageGeometry` takes a page's MediaBox, CropBox and `/Rotate`, and:

- computes the visible area: the CropBox clipped to the MediaBox, or the MediaBox when there is no CropBox or it lies outside;
- gives the displayed size (width and height swapped for 90 and 270);
- converts screen coordinates (pixels on the rendered page, origin at the top left, `scale` pixels per point) to user space and back, for points and rectangles;
- exposes the `displayedToUser()` matrix, which also makes content drawn in displayed orientation appear upright on a rotated page.

`rotationFromDegrees` reads `/Rotate` the way PDFium does (truncated to a multiple of 90, then brought into [0, 360)), so that placement follows what Qt PDF displays even for unusual values.

The unit tests check, among others, every displayed corner of every page listed in `tests/fixtures/pdf/manifest.json` at three zoom levels. They pass in CI.

## The harness

`placement.cpp` takes PDFs and, for every page, 4 zoom levels (0.5, 1, 1.5, 2 pixels per point) and 2 ways of using PoDoFo:

1. reads the page geometry with PoDoFo (`GetMediaBoxRaw`, `GetCropBoxRaw`, `TryGetRotationRaw`);
2. selects a rectangle on the displayed page (20 to 60 % of the width, 55 to 70 % of the height: asymmetric on purpose);
3. converts it to user space with `PageGeometry::toUser`;
4. draws it with PoDoFo as a `/Stamp` annotation (as decided in PHY-82): a red box with a black square in its displayed top-left corner;
5. renders the page with Qt PDF (`RenderFlag::Annotations`) at the same zoom level, and compares:
   - the page size reported by Qt PDF with the displayed size computed by the domain;
   - the bounding box of the red pixels with the selection (tolerance: 1.5 px per edge, for the rounded image size);
   - the position of the black square, which must be in the top-left quarter of the box (the drawing is upright).

The two ways of using PoDoFo:

- `raw`: `SetRectRaw` with the user-space rectangle, and a form XObject drawn in displayed orientation whose `/Matrix` is the rotation part of `displayedToUser()`, set with `SetAppearanceStreamRaw`;
- `naive`: PoDoFo's convenience API (`CreateAnnot(Rect)`, `SetAppearanceStream`) fed with the same user-space rectangle.

### Running it

In the dev container:

```sh
cmake -S spikes/phy-83-placement -B build-container/spike-83 -G Ninja
cmake --build build-container/spike-83
cd tests/fixtures/pdf
QT_QPA_PLATFORM=offscreen ../../../build-container/spike-83/placement ../../../build-container/spike-83/out *.pdf
```

The output PDFs and one PNG per page are written to `build-container/spike-83/out/`.

## Results

All observed with PoDoFo 1.1.2 and Qt PDF 6.11.3, on the 11 PDFs of the corpus (15 pages: A4 and Letter, portrait and landscape, `/Rotate` 0/90/180/270, offset CropBox, MediaBox with a negative origin, signed, PDF/A):

| Mode | Placements | Passed | Max edge error |
|---|---|---|---|
| `raw` | 60 (15 pages × 4 zoom levels) | **60** | 0.80 px |
| `naive` | 60 | 36: every unrotated page, none of the 24 rotated ones | 105 to 673 px on rotated pages |

- **Qt PDF's page size** (`QPdfDocument::pagePointSize`) matches the domain's displayed size on every page: it is the visible area, turned by `/Rotate`.
- **Unusual `/Rotate` values**: pages derived from `a4-rotate-90.pdf` with `/Rotate` set to -90, 450, 135, 45 and -45 (rewritten with `qpdf`, not in the corpus) are displayed by Qt PDF as 270, 90, 90, 0 and 0, and the `raw` placement passes on all of them (20 of 20). This confirms the PDFium reading implemented by `rotationFromDegrees`.
- **PoDoFo's convenience API adjusts coordinates for the page rotation** (`adjustRectToCurrentRotation` in `PdfPage`, and the canvas rotation alignment in `PdfPainter`). Fed with user-space coordinates, it misplaces and turns the box on every rotated page, and it also disagrees with Qt PDF on invalid values such as `/Rotate 45`. The adapter must use the raw APIs.

### Reusable image comparison

The `measure` function in `placement.cpp` is the approach to reuse in integration tests: render a page with Qt PDF at a known scale, find the bounding box of a solid-colour mark near where it is expected, and compare it with the expected screen rectangle, with a tolerance of about one pixel for the rounded image size. An orientation marker in one corner of the mark detects flipped or turned placements that a bounding box alone would miss.

### Not covered

- `/Rotate` inherited from the page tree rather than set on the page: PoDoFo's raw getter is expected to follow inheritance, but no fixture has it.
- `UserUnit` (PDF 1.6), which scales user space: none of the fixtures uses it.
- High-DPI screens: the caller must fold the device pixel ratio into `scale`.

## Recommendation

- Move on with `PageGeometry` as it is: it is already in `src/domain/` with its unit tests.
- In the PoDoFo adapter, read boxes and rotation with `GetMediaBoxRaw`, `GetCropBoxRaw` and `TryGetRotationRaw`, and place annotations with `SetRectRaw` and `SetAppearanceStreamRaw`, the form XObject carrying the rotation part of `displayedToUser()`.
- Add pages with inherited `/Rotate`, unusual `/Rotate` values and `UserUnit` to the corpus before writing the adapter.
