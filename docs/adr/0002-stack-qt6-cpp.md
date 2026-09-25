# 0002 — Application stack: Qt 6 + C++

- **Status**: accepted
- **Date**: 2026-09-25

## Context

Desktop application for Windows and Linux (macOS postponed), with stylus input (pressure) and accurate PDF rendering.

## Decision

- Qt 6 + C++, built with CMake. Qt version and installation: see 0006.
- Development in VS Code, without Qt Creator.
- QML user interface (proposed).
- Unit and integration tests with Qt Test, run by CTest.
- PDF libraries: Qt PDF for rendering, PoDoFo for writing (see 0005).

## Consequences

- Qt provides a cross-platform pointer API (stylus pressure).
- The PDF library choice is detailed in ADR 0005.
- Dependencies must remain GPLv3-compatible (see 0003).
