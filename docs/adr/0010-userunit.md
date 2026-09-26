# 0010 — Pages with a UserUnit

- **Status**: accepted
- **Date**: 2026-09-26

## Context

`/UserUnit` (PDF 1.6, ISO 32000-1 table 30) scales user space: one unit is `UserUnit`/72 inch instead of 1/72 inch. It is meant for pages larger than the 200-inch limit of PDF sizes, such as architectural drawings and banners, and is rare in documents people sign.

Misign places the signature in user space (`PageGeometry`, ADR 0005), from a box the user draws on the page as Qt PDF displays it. The stroke width, still to be written in the domain, is the only size that could be expressed in absolute terms.

Findings, each labelled as observed or read:

- **Observed** (PHY-87, `tests/fixtures/pdf/a4-userunit-2.pdf`, `/UserUnit 2` and a MediaBox of 297.638 × 420.945 units): Qt PDF 6.11 ignores `UserUnit`. `QPdfDocument::pagePointSize` gives 297.638 × 420.945, half of A4, and the page is rendered at that size. Ghostscript honours it and renders the page A4.
- **Read in the source, not run**: PoDoFo 1.1.2 has no `UserUnit` API. Nothing in its sources mentions the key, so the adapter could only read it from the page dictionary.
- **Not observed**: Adobe Acrobat Reader's behaviour.

## Options considered

- **Ignore `UserUnit`**: work in user units everywhere, as Qt PDF displays the page. The signature keeps the size and stroke width it has relative to the page on screen, in every reader. In a reader that honours `UserUnit`, the page and the signature are scaled together, so the signature is `UserUnit` times larger on paper than a signature of the same on-screen size on a regular page. No code is needed.
- **Scale stroke widths by 1/`UserUnit`**: strokes keep their physical width on paper. But they then look `UserUnit` times thinner relative to the page than in Misign's preview, and Qt PDF, which ignores `UserUnit`, shows them thinner too once saved. The adapter would have to read the key from the raw dictionary.
- **Warn the user, or refuse the page**: the user cannot act on the warning, and refusing blocks signing documents that display correctly.

## Decision

Ignore `UserUnit`. Misign works in user-space units from end to end, and the PDF adapter neither reads nor writes `/UserUnit`. A saved signature looks the way it did in Misign relative to the page, in every reader, whether it honours `UserUnit` or not.

## Consequences

- The signature is never distorted relative to the page, and the domain and the adapter need no special case.
- On a `UserUnit` page the physical size of the signature on paper differs from the same signature on a regular page, by the same factor as the page itself. Given how rare these pages are in documents to sign, this is accepted.
- At 100 % zoom such a page appears at its size in user units, not its physical size, as in any Qt PDF viewer.
- The PoDoFo adapter's integration tests must include `a4-userunit-2.pdf` and check that the drawn box lands where it was placed, in user space, with `/UserUnit` left unchanged.
- Acrobat Reader was not checked. If it turns out to treat these pages in a way that breaks this reasoning, the decision should be reopened.
