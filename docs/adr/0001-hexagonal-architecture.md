# 0001 — Hexagonal architecture

- **Status**: proposed
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

Stroke data model:

- `Point`: x, y, pressure (0 to 1, constant in touchpad / mouse mode), timestamp
- `Stroke`: sequence of points between pen down and pen up
- `Signature`: list of strokes, input mode, bounding box
- `Placement`: page number and rectangle in PDF coordinates (points, origin at bottom left)

Strokes stay vector data end to end.

## Consequences

- The domain depends on neither Qt nor any PDF library.
- Switching PDF library means writing a new adapter.
- A signature can be resampled to any box size, and later saved for reuse.
