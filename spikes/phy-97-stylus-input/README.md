# PHY-97 spike: stylus, touchpad and mouse input with Qt 6.11

Throwaway probe answering one question before the Drawing milestone: does Qt 6.11 give Misign usable pressure and a dense enough point stream from a stylus, and a usable stream from a touchpad and a mouse, on Windows and Linux? It is not part of the Misign build and is not maintained.

**Answer, Windows: yes for the stylus**, through Windows Ink (Qt's default), provided the GUI thread stays light while drawing. Touchpad, mouse and Linux are not observed yet (PHY-109). Details below; each finding says whether it was observed with the probe or read in documentation or source code.

## The probe

`stylus-probe` is a Qt Quick window that records every pointer event reaching the window, through an event filter installed on the `QQuickWindow`: it sees events before Qt Quick delivers them. For each event point it logs the event type, the device (name, type, pointer type, capabilities), the window and the global (screen) position, pressure, tilt, rotation, buttons, the event timestamp and a steady-clock reception time. It also logs what QML handlers report: a `PointHandler` per device class, and a `HoverHandler` for the mouse and the touchpad.

"Drawing" follows Misign's input model: the stylus draws while it touches the surface; the mouse and the touchpad draw as a toggle, one click starting a stroke and the next click ending it, with no button held. Mouse events Qt synthesizes from the stylus never toggle drawing.

By default the probe never accepts tablet events, so Qt Quick still delivers them and Qt synthesizes a mouse event from each (findings 6 and 7). `--accept-tablet` accepts and consumes them, as Misign's capture code will.

The window draws the raw strokes incrementally into a cached image, and shows a per-device summary whose statistics are kept up to date as samples arrive, so its cost does not grow with the session. Closing it writes `phy97-<platform>-<mode>-<time>.csv` and a `.txt` summary. Keys: T and M mark the start of touchpad and mouse input in the log (Qt reports both as the same device on Windows), C clears, S saves, P toggles the point markers.

Options: `--no-compress` turns off `Qt::AA_CompressTabletEvents` and `Qt::AA_CompressHighFrequencyEvents`; `--accept-tablet` accepts tablet events; `--wintab` (Windows) switches from Windows Ink to Wintab.

## Running it

Built on its own, never part of the Misign build:

```sh
cmake -S spikes/phy-97-stylus-input -B build-spike-97 -G Ninja && cmake --build build-spike-97
```

The `Spike stylus probe` workflow (`.github/workflows/spike-stylus.yml`) builds it for Windows (MSVC, `windeployqt`) and as a Linux AppImage (X11 and Wayland), uploaded as the `stylus-probe-windows` and `stylus-probe-linux` artifacts. Stylus input must be tested on the host: under WSL or in the dev container, the stylus driver stays on the Windows side.

## Results

### Windows 11, XP-Pen tablet, Qt 6.11.3

Observed with the probe on 2026-09-26, unless a finding says it was read in the Qt sources (`qtbase`, `6.11` branch). Those runs predate `--accept-tablet` and the global position column: the probe never accepted tablet events. The first run was discarded: the probe then redrew every sample on each frame, the GUI thread fell behind, and the measured rates reflected the probe, not Qt (see finding 3).

| Mode | Tablet events while drawing | Longest gap | Pressure | Other |
|---|---|---|---|---|
| Windows Ink, Qt defaults | ~212 per second | 15 ms | yes | tilt, hover |
| Windows Ink, `--no-compress` | ~212 per second | 22 ms | yes | tilt, hover |
| Wintab (`--wintab`) | none | | no | the pen arrives as plain mouse events |

1. **Windows Ink gives everything the stylus needs.** Qt's default backend (`WM_POINTER`, device name `wmpointer`, type `Stylus`, pointer type `Pen`) reports pressure, X and Y tilt, and hover moves (the pen tracked above the surface). Rotation is always 0. Pressure values are multiples of 1/1024 (e.g. 63/1024, 688/1024); the first contact reads about 0.05 to 0.06, and the highest value seen was 0.69. The 1024 levels are the range of the Windows Ink API, not of the tablet: Qt divides `POINTER_PEN_INFO::pressure` (0 to 1024) by 1024 (read in `qwindowspointerhandler.cpp`). An 8192-level tablet, as XP-Pen tablets usually are, is capped at 1024 levels through Windows Ink.
2. **About 212 tablet events per second while drawing**, the tablet's report rate, with no gap above 22 ms. Dense enough for a smooth stroke once smoothed (PHY-102).
3. **The GUI thread must stay light while drawing.** With the first version of the probe, which redrew all samples on every frame, the default mode dropped to about 21 events per second with 75 ms gaps: Qt compressed the tablet events that queued up while the GUI thread was busy. Once the probe drew only new samples, compressed and uncompressed modes gave the same rate. Mouse moves suffered too; Windows itself coalesces `WM_MOUSEMOVE` when an application does not keep up (documented behaviour of the Windows message queue).
4. **Event timestamps are too coarse for speed.** `QEvent::timestamp()` moves in steps of 15 to 16 ms, and most consecutive events share the same timestamp, while they arrive about every 5 ms. Qt passes the message time (`msg.time`, the Windows tick) to `handleTabletEvent` (read in `qwindowspointerhandler.cpp`). A speed-based stroke width (PHY-103) needs its own steady clock at reception, and even then events arrive in small bursts.
5. **Window positions are whole logical pixels; global positions should not be.** Every logged window position (`scenePosition`) was a whole number. That is how Qt builds it: the pen's local position is a `QPoint` computed from `ptPixelLocation`, while the global position (`hiResGlobalPos`) is a `QPointF` computed from `ptHimetricLocation` (read in `qwindowspointerhandler.cpp`). So `globalPosition()` should carry sub-pixel precision, mapped back to the window with `QWindow::mapFromGlobal(QPointF)`. Read in the source only: the probe now logs global positions, to be observed in PHY-109.
6. **Unaccepted tablet events get a mouse twin.** Because the probe never accepted tablet events, Qt synthesized a mouse event from each, with the stylus device (type `Stylus`) and the pressure. That is Qt's rule: it synthesizes the mouse event only when the `QTabletEvent` was not accepted and `Qt::AA_SynthesizeMouseForUnhandledTabletEvents` is set (read in `QGuiApplicationPrivate::processTabletEvent`); the Windows plugin itself drops the mouse messages Windows generates from the pen (read in `qwindowspointerhandler.cpp`). Capture code that accepts the tablet events should get no twin; to be observed with `--accept-tablet` in PHY-109. In the logs, the twins never started mouse drawing, since their device type is `Stylus`.
7. **QML `PointHandler`s, as used here, are not a faithful stream.** With tablet events unaccepted, the stylus `PointHandler` reported 1543 active points for 779 tablet events in one run (about twice as many: it saw both the tablet events and their mouse twins) and 315 for 410 in another. It must not be used to capture strokes.
8. **Wintab gives no tablet events here.** Qt 6.11 has no platform option for Wintab any more: `windows:nowmpointer` is reported as an unknown option and ignored (read in `qwindowsintegration.cpp`, and observed: the device stayed `wmpointer`). The probe turns it on with the private `QWindowsApplication::setWinTabEnabled`, which succeeded, yet no tablet event arrived: the pen came as plain mouse events without pressure, at about 700 per second, and its contacts toggled mouse drawing. `WinTab32.dll` is installed. Whether the XP-Pen driver's Windows Ink setting explains it was not checked.

Not observed on Windows: tablet events accepted (`--accept-tablet`), global positions, the touchpad (none on the test machine), the mouse in toggle mode (only in the discarded first run), the barrel button and the eraser (only the pen tip was reported; the pen was not checked for them).

### Linux

Not observed yet: the probe runs as an AppImage on X11 and Wayland (checked under WSLg, without a stylus), and waits for a run on an Ubuntu machine with the tablet. That run, and the touchpad and mouse checks on Windows, are tracked in PHY-109.

## Recommendation for `IInputSource` (PHY-101)

- **Windows**: use Qt's default Windows Ink backend. Do not enable Wintab.
- **Capture in C++ and accept the `QTabletEvent`s**, so Qt synthesizes no mouse twin from them. Take the stylus from `QTabletEvent` and the mouse and the touchpad from `QMouseEvent`, with an event filter on the window restricted to the drawing area: it is the source observed to be complete. Whether a `QQuickItem` subclass receives the same stream was not tested; check it when writing the adapter. Do not capture with `PointHandler`.
- **Ignore mouse events whose device is a stylus**, as a safety net for the mouse and touchpad toggle in case a twin still comes through.
- **Take positions from `globalPosition()`**, mapped to the window with `QWindow::mapFromGlobal(QPointF)`, for sub-pixel precision; pending confirmation in PHY-109. If it turns out whole-pixel too, smoothing (PHY-102) must handle the stair steps.
- **Turn off `Qt::AA_CompressTabletEvents`**: at about 212 events per second it costs nothing, and a slow frame then never merges points. Keep drawing incremental anyway, so the GUI thread never falls behind.
- **Timestamp samples on reception with a steady clock**, not with `QEvent::timestamp()`. If reception times prove too bursty for the speed-based width (PHY-103), Windows Ink has a precise clock Qt drops: `POINTER_INFO::PerformanceCount` (a QueryPerformanceCounter value), readable from a native event filter on `WM_POINTERUPDATE`.
