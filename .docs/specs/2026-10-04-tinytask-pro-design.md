# TinyTask Pro System Design Specification

- **Date**: 2026-10-04
- **Project**: TinyTask Pro (Lightweight Win32 Automation)
- **Target Footprint**: < 1.0 MB (Pure C / Native Win32 / Zero Heavy Dependencies)
- **Baseline Version**: TinyTask 1.77 (Reversed clean-room C source preserved intact)

---

## 1. Executive Summary & Goals

TinyTask Pro is an advanced, ultra-lightweight Windows desktop automation tool inspired by Microsoft Power Automate, while retaining the minimalist, standalone, high-performance DNA of TinyTask (< 1MB single binary, zero external runtime dependencies like Python or .NET).

### Key Pillars
1. **Preserve Baseline**: Keep the reversed 1.77 original C source (`reverse-gemini/src/tinytask.c`, ~44KB binary) intact. TinyTask Pro is built as a dedicated Pro target (`tinytask_pro.c` / `tinytask_pro.exe`).
2. **Adaptive Visual & Element Capture**: Automatic edge detection and button bounding box cropping on click, paired with native Windows UI Automation (UIA) for text extraction.
3. **Multi-Match Spatial Disambiguation**: When multiple controls match target text on screen, select the candidate with the minimum Euclidean distance to the original recorded coordinates.
4. **Action Deconstruction & Semantic Synthesizer**: Record clean actions (Click, Drag, TypeText, Hotkey) rather than thousands of noisy mouse moves.
5. **Per-Step Configurable Timeout & Fallback**: Default 3.0s timeout per step, customizable in the UI. Graceful timeout modal prompting [Retry], [Use Recorded Pos], [Skip], or [Stop].
6. **Collapsible Drawer UI**: Maintain the classic 263×32 pixel floating ribbon by default, smoothly expanding downward into a 380×360 workflow drawer with a native Win32 ListView.
7. **Single-File Project Format (`.ttp`)**: Pack steps metadata and cropped BMP bitmaps into a single self-contained project package.

---

## 2. User Interface & Interaction Design

### 2.1 Window Dimensions & Collapsible Drawer
The UI remains strictly English and adheres to the classic TinyTask pixel toolbar aesthetic.

* **Collapsed State (Toolbar Only)**: `263 × 32 px` (or height with titlebar ~54px).
  Buttons: `[Open] [Save] [Rec] [Play] [Steps ▼] [Options]`
* **Expanded State (Drawer Open)**: `380 × 360 px`.
  Clicking `[Steps ▼]` expands the window downward using `SetWindowPos`, revealing the native `SysListView32` workflow list and quick action bar.

```
+--------------------------------------------------------------+
| [Open] [Save] [Rec] [Play] [Steps ▲] [Options]               |
+--------------------------------------------------------------+
| Workflow Steps (SysListView32):                              |
| +----+----------+----------------+------------+------------+ |
| | #  | Action   | Target / Text  | Timeout(s) | Asset      | |
| +----+----------+----------------+------------+------------+ |
| | 01 | Click    | [OK] (Text)    | 3.0 (Def)  | [Icon/BMP] | |
| | 02 | Drag     | (120,40)->...  | 5.0 (Cust) | [Path]     | |
| | 03 | TypeText | "admin123"     | 1.5 (Cust) | [Key]      | |
| +----+----------+----------------+------------+------------+ |
| Quick Bar: [Add] [Delete] [Move Up] [Move Down] [Step Run >] |
+--------------------------------------------------------------+
```

### 2.2 In-Place Editing & Preview
* **Double-click on "Timeout" column**: In-place edit box to change step-specific timeout (seconds).
* **Hover on "Asset" column**: Native tooltip displays thumbnail of the adaptively cropped button bitmap.
* **Step Run (`[Step Run >]`)**: Executes only the selected step to verify element location without replaying the entire sequence.

---

## 3. Element Detection & Dynamic Recognition Engine

### 3.1 Dual-Track Capture Pipeline (Recording)
On `WM_LBUTTONDOWN`:
1. **Track 1: Windows Native UI Automation (UIA) & Accessibility**:
   - Query `AccessibleObjectFromPoint` / `IUIAutomation::ElementFromPoint`.
   - Retrieve element `Name` (text) and `BoundingRectangle`.
   - If clear text exists, assign `targetMode = TARGET_TEXT` and record `textKey`.
2. **Track 2: Pure-C Adaptive Edge Detection (Image Anchor)**:
   - Capture a $256 \times 256$ ROI bitmap centered on cursor $(x, y)$ via GDI `BitBlt`.
   - Apply grayscale conversion and a 3×3 Sobel gradient operator.
   - Run radial/cross ray-casting outwards from $(x, y)$ to identify gradient boundaries forming a convex rectangular button outline.
   - Crop the boundary (clamped to sensible min/max, e.g. $24 \times 16$ to $200 \times 80$).
   - Save the cropped 24-bit uncompressed BMP into memory as the visual template.

### 3.2 Dynamic Playback & Spatial Disambiguation
During playback for a target step:
1. **Text Matching**:
   - Query UIA tree in the foreground/desktop window for matching `textKey`.
   - **Euclidean Nearest Neighbor Disambiguation**:
     If $K$ candidates $\{P_1, P_2, \dots, P_k\}$ match `textKey`, calculate Euclidean distance to the original recorded coordinate $(X_{orig}, Y_{orig})$:
     $$D_i = \sqrt{(X_i - X_{orig})^2 + (Y_i - Y_{orig})^2}$$
     Select $\arg\min_i D_i$.
2. **Pure-C Template Matching (NCC)**:
   - If `targetMode == TARGET_IMAGE` or UIA returns no match:
   - Execute pure-C Normalized Cross-Correlation (NCC) across the active window DC.
   - If peak correlation score $\ge 0.85$, compute bounding box center $(X_{found}, Y_{found})$ as the click target.
3. **Pure-C Implementation Footprint**:
   - Sobel + ray casting + NCC matcher implemented in ~400 lines of standard C without CRT/OpenCV bloat (~20KB binary footprint).

---

## 4. Semantic Recording & Action Synthesizer

Rather than polling 10ms differential coordinates for wandering mouse movement, TinyTask Pro implements an Action Synthesizer:
1. **Clicks**: `Down` + `Up` within 5px threshold and < 500ms -> `ACTION_CLICK`.
2. **Drags**: `Down` at $(X_1, Y_1)$ -> significant travel -> `Up` at $(X_2, Y_2)$ -> `ACTION_DRAG`.
3. **Key Inputs**: Consecutive `WM_KEYDOWN` within 1000ms aggregated into string buffer -> `ACTION_TYPE_TEXT`.
4. **Hotkeys**: Key combinations involving Ctrl/Alt/Shift/Win -> `ACTION_HOTKEY`.

---

## 5. Playback Pipeline, Timeouts & Failure Recovery

### 5.1 Step Execution Flow
For each step:
1. Initialize timer $t_0$.
2. Poll for target element every 200ms.
3. If found within $t \le \text{timeoutMs}$, dispatch mouse/keyboard injection via `SendInput` or `mouse_event` / `keybd_event`.
4. Sleep `postDelayMs` (default 100ms) before progressing to next step.

### 5.2 Timeout Modal Dialog
If $t > \text{timeoutMs}$, pause playback and pop up a native Win32 modal dialog:
```
+------------------------------------------------------------+
| Target Not Found - Step #N                                 |
+------------------------------------------------------------+
| Target "[Button Text]" could not be found within X.X s.   |
| Original recorded coordinate: (X, Y)                       |
|                                                            |
|    [ Retry ]   [ Use Recorded Pos ]   [ Skip ]   [ Stop ]  |
+------------------------------------------------------------+
```
* **Retry**: Reset step timer and resume scanning.
* **Use Recorded Pos**: Fallback to clicking $(X_{orig}, Y_{orig})$.
* **Skip**: Increment step index without executing action.
* **Stop**: Abort playback immediately.

---

## 6. Single-File Project Package Format (`.ttp`)

TinyTask Pro uses a single-file binary container (`.ttp`):

### 6.1 Header (16 bytes)
```c
typedef struct {
    char  magic[4];       /* "TTP1" */
    DWORD version;        /* 1 */
    DWORD stepCount;      /* N */
    DWORD flags;          /* Reserved */
} TTPHeader;
```

### 6.2 Step Descriptor (`TTPStep`)
```c
typedef struct {
    DWORD stepId;         /* 1-based index */
    DWORD actionType;     /* 1=Click, 2=DblClick, 3=RClick, 4=Drag, 5=TypeText, 6=Hotkey */
    DWORD targetMode;     /* 1=Text, 2=Image, 3=Coord */
    LONG  origX;          /* Recorded X coordinate */
    LONG  origY;          /* Recorded Y coordinate */
    LONG  destX;          /* Drag destination X */
    LONG  destY;          /* Drag destination Y */
    DWORD timeoutMs;      /* Configurable search timeout (ms) */
    DWORD postDelayMs;    /* Wait delay after action (ms) */
    char  textKey[128];   /* Text key or typed string */
    DWORD imageOffset;    /* Byte offset to bitmap in assets blob */
    DWORD imageSize;      /* Byte size of bitmap (0 if none) */
} TTPStep;
```

### 6.3 Assets Payload Blob
Appended after step descriptors array: compact 24-bit uncompressed `.bmp` binary streams sequentially packed.

---

## 7. Verification & Testing Strategy

1. **Compilation & Size Gate**:
   - Compile `tinytask_pro.exe` with MinGW GCC `-Os -s -mwindows`.
   - Verify final `.exe` binary size is $< 800 \text{ KB}$ (well under the 1 MB budget).
2. **Visual & UI Verification**:
   - Test drawer expand/collapse animation and layout alignment.
   - Verify ListView items rendering, custom timeouts, and thumbnail previews.
3. **Element Detection Verification**:
   - Test on standard Win32 dialogs (Calculator, Notepad, Explorer).
   - Test nearest-neighbor disambiguation with multiple identical "OK" buttons.
   - Test pure-C edge detection on non-standard styled buttons.
4. **Playback & Fallback Verification**:
   - Simulate delayed element appearance (verify polling succeeds before timeout).
   - Simulate missing element (verify timeout modal appears with [Retry], [Use Recorded Pos], [Skip], [Stop]).
5. **Format Verification**:
   - Save and reload `.ttp` project packages, ensuring image blobs and step parameters round-trip accurately.
