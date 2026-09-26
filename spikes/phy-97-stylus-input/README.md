# PHY-97 spike: stylus, touchpad and mouse input with Qt 6.11

Throwaway probe answering one question before the Drawing milestone: does Qt 6.11 give Misign usable pressure and a dense enough point stream from a stylus, and a usable stream from a touchpad and a mouse, on Windows and Linux? It is not part of the Misign build and is not maintained.

**Answer, Windows: yes for the stylus**, through Windows Ink (Qt's default), provided the GUI thread stays light while drawing. Touchpad, mouse and Linux are not observed yet. Details below; each finding says whether it was observed with the probe or read in documentation or source code.

## The probe

`stylus-probe` is a Qt Quick window that records every pointer event reaching the window, through an event filter installed on the `QQuickWindow`: it sees events before Qt Quick delivers them. For each event point it logs the event type, the device (name, type, pointer type, capabilities), position, pressure, tilt, rotation, buttons, the event timestamp and a steady-clock reception time. It also logs what QML handlers report: a `PointHandler` per device class, and a `HoverHandler` for the mouse and the touchpad.

"Drawing" follows Misign's input model: the stylus draws while it touches the surface; the mouse and the touchpad draw as a toggle, one click starting a stroke and the next click ending it, with no button held. Mouse events Qt synthesizes from the stylus never toggle drawing.

The window draws the raw strokes incrementally into a cached image, and shows a per-device summary. Closing it writes `phy97-<platform>-<mode>-<time>.csv` and a `.txt` summary. Keys: T and M mark the start of touchpad and mouse input in the log (Qt reports both as the same device on Windows), C clears, S saves, P toggles the point markers.

Options: `--no-compress` turns off `Qt::AA_CompressTabletEvents` and `Qt::AA_CompressHighFrequencyEvents`; `--wintab` (Windows) switches from Windows Ink to Wintab.

## Running it

Built on its own, never part of the Misign build:

```sh
cmake -S spikes/phy-97-stylus-input -B build-spike-97 -G Ninja && cmake --build build-spike-97
```

The `Spike stylus probe` workflow (`.github/workflows/spike-stylus.yml`) builds it for Windows (MSVC, `windeployqt`) and as a Linux AppImage (X11 and Wayland), uploaded as the `stylus-probe-windows` and `stylus-probe-linux` artifacts. Stylus input must be tested on the host: under WSL or in the dev container, the stylus driver stays on the Windows side.

## Results

### Windows 11, XP-Pen tablet, Qt 6.11.3

All observed with the probe, 2026-09-26. The first run was discarded: the probe then redrew every sample on each frame, the GUI thread fell behind, and the measured rates reflected the probe, not Qt (see finding 3).

| Mode | Tablet events while drawing | Longest gap | Pressure | Other |
|---|---|---|---|---|
| Windows Ink, Qt defaults | ~212 per second | 15 ms | yes | tilt, hover |
| Windows Ink, `--no-compress` | ~212 per second | 22 ms | yes | tilt, hover |
| Wintab (`--wintab`) | none | | no | the pen arrives as plain mouse events |

1. **Windows Ink gives everything the stylus needs.** Qt's default backend (`WM_POINTER`, device name `wmpointer`, type `Stylus`, pointer type `Pen`) reports pressure, X and Y tilt, and hover moves (the pen tracked above the surface). Rotation is always 0. Pressure values are consistent with 1024 levels (e.g. 63/1024, 688/1024); the first contact reads about 0.05 to 0.06, and the highest value seen was 0.69.
2. **About 212 tablet events per second while drawing**, the tablet's report rate, with no gap above 22 ms. Dense enough for a smooth stroke once smoothed (PHY-102).
3. **The GUI thread must stay light while drawing.** With the first version of the probe, which redrew all samples on every frame, the default mode dropped to about 21 events per second with 75 ms gaps: Qt compressed the tablet events that queued up while the GUI thread was busy. Once the probe drew only new samples, compressed and uncompressed modes gave the same rate. Mouse moves suffered too; Windows itself coalesces `WM_MOUSEMOVE` when an application does not keep up (documented behaviour of the Windows message queue).
4. **Event timestamps are too coarse for speed.** `QEvent::timestamp()` moves in steps of 15 to 16 ms (the Windows tick), and most consecutive events share the same timestamp, while they arrive about every 5 ms. A speed-based stroke width (PHY-103) needs its own steady clock at reception, and even then events arrive in small bursts.
5. **Positions are whole logical pixels.** No sub-pixel precision in any event, at the display scale used. Smoothing (PHY-102) has to deal with the resulting stair steps.
6. **Qt synthesizes a mouse event for each tablet event**, and gives it the stylus device (type `Stylus`) and the pressure. So synthesized mouse events are told apart from real mouse events by their device type, and the toggle rule is easy to apply: in the logs, pen contacts never started mouse drawing.
7. **QML `PointHandler`s are not a faithful stream.** The stylus `PointHandler` reported 1543 active points for 779 tablet events in one run (it sees both the tablet events and the synthesized mouse events) and 315 for 410 in another. It must not be used to capture strokes.
8. **Wintab gives no tablet events here.** Qt 6.11 has no platform option for Wintab any more: `windows:nowmpointer` is reported as an unknown option and ignored (read in `qwindowsintegration.cpp`, and observed: the device stayed `wmpointer`). The probe turns it on with the private `QWindowsApplication::setWinTabEnabled`, which succeeded, yet no tablet event arrived: the pen came as plain mouse events without pressure, at about 700 per second, and its contacts toggled mouse drawing. `WinTab32.dll` is installed. Whether the XP-Pen driver's Windows Ink setting explains it was not checked.

Not observed on Windows: the touchpad (none on the test machine), the mouse in toggle mode (only in the discarded first run), the barrel button and the eraser (only the pen tip was reported; the pen was not checked for them).

### Linux

Not observed yet: the probe runs as an AppImage on X11 and Wayland (checked under WSLg, without a stylus), and waits for a run on an Ubuntu machine with the tablet.

## Recommendation for `IInputSource` (PHY-101)

- **Windows**: use Qt's default Windows Ink backend. Do not enable Wintab.
- **Capture in C++**, from `QTabletEvent` for the stylus and `QMouseEvent` for the mouse and the touchpad, with an event filter on the window restricted to the drawing area: it is the source observed to be complete. Whether a `QQuickItem` subclass receives the same stream (through the synthesized mouse events, which carry the pressure) was not tested; check it when writing the adapter. Do not capture with `PointHandler`.
- **Ignore mouse events whose device is a stylus** for the mouse and touchpad toggle.
- **Turn off `Qt::AA_CompressTabletEvents`**: at about 212 events per second it costs nothing, and a slow frame then never merges points. Keep drawing incremental anyway, so the GUI thread never falls behind.
- **Timestamp samples on reception with a steady clock**, not with `QEvent::timestamp()`.
- Treat positions as integer-grained input for smoothing.
