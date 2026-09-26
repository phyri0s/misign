# 0001 — Hexagonal architecture

- **Status**: accepted (data model implemented in PHY-98, 2026-09-26)
- **Date**: 2026-09-25

## Context

Misign depends on technologies that may change: PDF library, UI framework, stylus input API. The business logic (stroke smoothing, stroke width, screen → PDF coordinate conversion) must remain testable without them.

## Decision

Hexagonal architecture (ports and adapters):

| Layer | Responsibility | Examples |
|---|---|---|
| Domain | Pure rules, no external dependencies, unit tested | Bézier smoothing, width from pressure or speed, screen → PDF coordinate conversion |
| Application | Use case orchestration | Open a document, place the signature box, sign, save a copy |
| Ports | Interfaces to the outside world | `IPdfRenderer`, `IPdfWriter`, `IInputSource`, `ISignatureStore` |
| Adapters | Stack-specific implementations | PDF library, pointer API, file system |
| UI | Display and interaction, no business logic | Screens, controls, shortcuts |

Stroke data model (`src/domain/point.h`, `stroke.h`, `signature.h`):

- `Point`: x, y on the drawing surface (logical pixels, origin at the top left, y down), pressure (0 to 1, constant for the mouse and the touchpad), timestamp in microseconds taken on reception with a steady clock (the event timestamps are too coarse, PHY-97)
- `Stroke`: the points of one stroke and its input mode (stylus, touchpad or mouse). The stylus draws while it touches the surface; the mouse and the touchpad draw as a toggle, a click starting the stroke and the next click ending it, with no button held
- `Signature`: list of strokes and their bounding box. `fitInto` gives the matrix that maps it into a box in PDF coordinates, scaled uniformly, centred and turned the right way up, so one drawing fits any box
- `Placement`: page number and rectangle in PDF coordinates (points, origin at bottom left)

The input mode is recorded per stroke rather than per signature: a signature can mix inputs, and the stroke width depends on it (pressure for the stylus, speed otherwise).

Strokes stay vector data end to end.

## Consequences

- The domain depends on neither Qt nor any PDF library.
- Switching PDF library means writing a new adapter.
- A signature can be resampled to any box size, and later saved for reuse.
