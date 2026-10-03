# TinyTask Pro

> **Next-Generation Ultra-Lightweight Desktop Automation & RPA**  
> *Pure C & Native Win32 | Zero External Dependencies | Single Standalone Binary (< 80 KB)*

---

## 1. Overview

**TinyTask Pro** is an evolutionary leap from the classic TinyTask macro recorder. While traditional Robotic Process Automation (RPA) tools like Microsoft Power Automate or UiPath require gigabytes of runtimes (.NET, Python, Electron, Chromium), **TinyTask Pro** delivers modern visual and semantic automation within an astonishing **77 KB standalone executable**.

TinyTask Pro retains the clean-room 1.77 classic source code untouched, introducing a parallel Pro target with computer vision, control text recognition, spatial disambiguation, an expandable workflow drawer, in-place step editing, and complete preferences parity.

---

## 2. Key Capabilities & Architectural Innovations

### 🖼️ 1. Pure-C Adaptive Edge Detection & Button Cropping
* **Automatic Region of Interest (ROI)**: On mouse click, captures a $256 \times 256$ pixel buffer around the cursor.
* **Sobel Gradient Edge Detection**: Runs a $3\times3$ Sobel filter ($G_x, G_y$) to compute gradient magnitudes.
* **Radial Ray-Casting**: Scans outwards in 4 orthogonal directions from the click point to lock onto closed high-contrast rectangular button boundaries.
* **Tight Template Generation**: Crops the button contour (clamped to realistic UI dimensions) and stores an uncompressed 24-bit DIB/BMP template in memory.

### 🎯 2. Dual-Track Recognition & Spatial Disambiguation
* **Track 1: Windows UI Text Extraction**:
  Automatically inspects control hierarchies and text labels (`"OK"`, `"Submit"`, `"Search"`, `"Save"`).
* **Euclidean Nearest-Neighbor Disambiguation**:
  In real-world interfaces with multiple identical labels (e.g., several "Cancel" or "Delete" buttons), the engine computes Euclidean distances to the originally recorded physical coordinate $(X_{orig}, Y_{orig})$:
  $$D_i = \sqrt{(X_i - X_{orig})^2 + (Y_i - Y_{orig})^2}$$
  The candidate with minimum distance is automatically selected.
* **Track 2: Pure-C Normalized Cross-Correlation (NCC)**:
  When targeting custom-drawn or icon-only buttons, runs a sliding-window NCC template matcher directly on the screen DC:
  $$\text{NCC} = \frac{\sum (T - \bar{T})(I - \bar{I})}{\sqrt{\sum (T - \bar{T})^2 \sum (I - \bar{I})^2}}$$
  Locks onto the peak correlation coordinate (confidence $\ge 0.85$).

### 📋 3. Semantic Action Synthesizer & Deconstructed Steps
* **Zero Jitter / No Mouse Wandering**: Discards continuous $10\text{ms}$ free mouse cursor movements, aggregating raw hardware input into clean semantic actions:
  * `Click`: Mouse down + up within 5px threshold.
  * `DblClick`: Two rapid clicks within double-click interval.
  * `RClick`: Right mouse click.
  * `Drag`: Origin $(x_1, y_1)$ to destination $(x_2, y_2)$.
  * `Type`: Consecutive keyboard keystrokes aggregated into clean text strings (`"Hello World"`).
  * `Hotkey`: Modified combinations (e.g., `Ctrl + S`, `Alt + F4`, `Enter`, `Tab`).

### 🗂️ 4. Collapsible Drawer UI (`SysListView32`)
* **Collapsed Mode (`263 × 54 px` or `263 × 42 px`)**: Preserves TinyTask's iconic compact floating ribbon toolbar.
* **Expanded Drawer Mode (`380 × 360 px`)**: Clicking `[Steps]` smoothly resizes the window, revealing an interactive native Windows ListView:
  * `#`: Step sequence number.
  * `Action`: Action type (`Click`, `Drag`, `Type`, `DblClick`, `RClick`, `Hotkey`).
  * `Target / Text`: Displaying target control label, typed text, or coordinate anchor.
  * `Timeout(s)`: Configurable step timeout (e.g., `3.0s (Def)` or `5.0s`).
  * `Asset`: Visual template indicator (`[BMP]` or `-`).
* **In-Place Editing**: Double-click any cell in the `Timeout(s)` column to open a floating text editor and customize the timeout value in seconds.
* **Step Manipulation Bar**: Quick buttons for `[+ Add]`, `[✕ Del]`, `[▲ Up]`, `[▼ Dn]`, and `[Step Run ▶]` (single-step test execution).

### ⏱️ 5. Per-Step Configurable Timeouts & Graceful Fallback
* Rather than a rigid global timeout, each step carries an independent `timeoutMs` attribute (default 3.0 seconds, configurable via Preferences).
* **Interactive Recovery Modal**: If an element is temporarily obscured or changed, playback pauses and presents a native dialog:
  * `[Retry]`: Resets step timer and scans again.
  * `[Use Recorded Pos]`: Fallback click on the originally recorded physical coordinates.
  * `[Skip]`: Skips this step and proceeds to the next.
  * `[Stop]`: Gracefully aborts playback.

### 📦 6. Single-File Self-Contained Project Packaging (`.ttp`)
* Saves workflows into a single `.ttp` (TinyTask Pro) file.
* Packed binary structure containing file header, step metadata array, and sequential uncompressed bitmap blobs.
* Fully portable with zero external image asset folders. Backward-compatible with legacy `.rec` files.

---

## 3. Comprehensive Preferences & Options

TinyTask Pro provides complete parity with classic TinyTask 1.77, along with Pro extensions:

| Menu Section | Options | Description |
|---|---|---|
| **Speed Modes** | `1/2x`, `1x`, `2x`, `100x` | Standard speed multipliers. |
| | `Play Custom Speed: %dx` | Active custom speed multiplier. |
| | `Set Custom Speed...` | Input dialog to set any multiplier between `1x` and `100x`. |
| **Loops & Repetition** | `Continuous Playback` | Infinite playback loop toggle. |
| | `Set Playback Loops... (%d)` | Input dialog to set specific loop counts (`1` to `999,999`). |
| **Recording Hotkeys** | `Ctrl + Shift + Alt + R` | Standard TinyTask combination. |
| | `Print Screen`, `F8`, `F12` | One-key quick recording triggers. |
| | `Custom: <Key>` | User-defined hotkey with any modifier combination. |
| | `Set Custom Hotkey...` | Interactive modal key capture dialog. |
| **Playback Hotkeys** | `Ctrl + Shift + Alt + P` | Standard playback trigger. |
| | `Print Screen`, `F8`, `F12` | One-key quick playback triggers. |
| | `Set Custom Hotkey...` | Interactive modal key capture dialog. |
| | `{PAUSE}` / `{ScrollLock}` | Emergency abort hotkeys (active at all times). |
| **View & Themes** | `Always on Top` | Keeps TinyTask Pro floating over all windows. |
| | `Show Captions` | Toggles button title text (compact 32px vs full 44px height). |
| | `Use Custom Toolbar...` | Loads custom 266×44 7-frame BMP toolbar skins. |
| | `Use Default Toolbar` | Restores default built-in bitmap. |
| **Pro Configurations**| `Set Default Step Timeout...`| Configures default search duration (default `3s`, range `1s - 60s`). |
| **Configuration File**| `tinytask_pro.ini` | Automatically stores and restores all settings and window positions. |

---

## 4. Comparison Matrix

| Feature | TinyTask 1.77 | TinyTask Pro | Power Automate / UiPath |
|---|---|---|---|
| **Binary Footprint** | ~35 KB | **~77 KB** | 500 MB ~ 2 GB |
| **External Dependencies**| None (Win32) | **None (Win32 / GDI)** | .NET, Python, Electron |
| **Element Locating** | Fixed Physical $(x,y)$ | **Dynamic Vision + Text Anchors** | Vision + Accessibility |
| **Multi-Match Handling**| N/A | **Euclidean Nearest Neighbor** | XPath / Selectors |
| **Step Deconstruction**| Raw 10ms stream | **Interactive Step Table** | Flowchart / Action Tree |
| **Step Timeout Config** | None | **Per-Step In-Place Edit** | Property Inspector |
| **Single-File Sharing** | `.rec` (coords only) | **`.ttp` (steps + embedded bitmaps)** | Multi-file directory / Zip |
| **Emergency Abort** | Pause / ScrollLock | **Pause / ScrollLock** | Hotkey / Tray Icon |

---

## 5. Source Code & Architecture

```
reverse-gemini/
├── bin/
│   ├── tinytask.exe           # Classic 1.77 clean-room binary (~44 KB)
│   └── tinytask_pro.exe       # TinyTask Pro optimized binary (~77 KB)
├── src/
│   ├── ttp_core.h             # Core data structures (TTPHeader, TTPStep)
│   ├── ttp_storage.h / .c     # .ttp binary container serialization engine
│   ├── ttp_vision.h / .c      # Pure-C Sobel edge detection & NCC template matcher
│   ├── ttp_engine.h / .c      # Semantic action synthesizer & playback engine
│   ├── tinytask_pro.rc        # Icon (4001), Toolbar (4002), and ComCtl6 manifest
│   ├── tinytask_pro.c         # Collapsible drawer UI, Preferences, and main loop
│   └── tinytask.c             # Classic clean-room 1.77 source (unmodified)
└── tests/
    ├── test_storage.c         # Storage serialization roundtrip tests
    ├── test_vision.c          # Pure-C Sobel & NCC vision unit tests
    ├── test_engine.c          # Synthesizer & playback engine tests
    └── test_tinytask_pro.c    # Full integration test suite
```

---

## 6. Build Instructions

### Prerequisites
* Windows 7 / 8 / 10 / 11
* MinGW-w64 GCC with standard Win32 SDK headers

### Compile Release Binary
```bash
# 1. Compile resource object with Common Controls 6 manifest
windres reverse-gemini/src/tinytask_pro.rc -O coff -o reverse-gemini/src/tinytask_pro_res.o

# 2. Compile standalone optimized executable
gcc -Os -s -mwindows \
    reverse-gemini/src/tinytask_pro.c \
    reverse-gemini/src/ttp_storage.c \
    reverse-gemini/src/ttp_vision.c \
    reverse-gemini/src/ttp_engine.c \
    reverse-gemini/src/tinytask_pro_res.o \
    -lcomctl32 -loleacc -lgdi32 -luser32 -lcomdlg32 -lshell32 -lm \
    -o reverse-gemini/bin/tinytask_pro.exe

# 3. Clean up intermediate object file
rm reverse-gemini/src/tinytask_pro_res.o
```

### Run Test Suites
```bash
# Storage test
gcc -Ireverse-gemini/src reverse-gemini/src/ttp_storage.c reverse-gemini/tests/test_storage.c -o test_storage.exe && ./test_storage.exe

# Vision engine test
gcc -Ireverse-gemini/src reverse-gemini/src/ttp_vision.c reverse-gemini/tests/test_vision.c -lgdi32 -lm -o test_vision.exe && ./test_vision.exe

# Synthesizer & execution engine test
gcc -Ireverse-gemini/src reverse-gemini/src/ttp_engine.c reverse-gemini/src/ttp_vision.c reverse-gemini/tests/test_engine.c -lgdi32 -luser32 -lm -o test_engine.exe && ./test_engine.exe

# TinyTask Pro integration test
gcc -Ireverse-gemini/src reverse-gemini/src/ttp_storage.c reverse-gemini/src/ttp_vision.c reverse-gemini/src/ttp_engine.c reverse-gemini/tests/test_tinytask_pro.c -lgdi32 -luser32 -lcomctl32 -lcomdlg32 -lshell32 -lm -o test_pro.exe && ./test_pro.exe
```

---

## 7. License

Built for high-performance automation and reverse-engineering research.  
All code adheres to clean-room software design principles.
