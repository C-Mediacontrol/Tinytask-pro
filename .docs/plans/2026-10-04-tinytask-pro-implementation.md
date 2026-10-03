# TinyTask Pro Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build TinyTask Pro, an ultra-lightweight (< 1MB) Windows desktop automation tool with adaptive visual/element detection, multi-match spatial disambiguation, per-step custom timeouts, and collapsible workflow drawer.

**Architecture:** Modular pure C architecture targeting native Win32/GDI. Core logic is isolated into distinct modules: data modeling & single-file package storage (`ttp_storage`), pure-C adaptive vision/NCC & UIA detection (`ttp_vision`), semantic action synthesis & playback engine (`ttp_engine`), and the collapsible drawer UI shell (`tinytask_pro.c`). Original `tinytask.c` remains untouched.

**Tech Stack:** C (C99/C11), Win32 API, GDI, `SysListView32` (Comctl32), `Oleacc`/`UIAutomationCore`, MinGW-w64 GCC (`-Os -s -mwindows`).

## Global Constraints

- Preserve original baseline: Do NOT modify or overwrite `reverse-gemini/src/tinytask.c` or `Library/tinytask.exe`.
- Binary size limit: Final compiled `bin/tinytask_pro.exe` must strictly be $< 800 \text{ KB}$ (well under the 1.0 MB limit).
- Language and style: All UI labels and modal text must be in English, matching classic TinyTask 1.77 aesthetic.
- Zero heavyweight runtime: No Python, .NET runtime, Electron, or OpenCV dependencies. Pure Win32 C only.
- Working directory rule: AI writes only into subdirectories (`reverse-gemini/`).

---

### Task 1: Core Data Models & `.ttp` Single-File Storage Module

**Files:**
- Create: `reverse-gemini/src/ttp_core.h`
- Create: `reverse-gemini/src/ttp_storage.h`
- Create: `reverse-gemini/src/ttp_storage.c`
- Test: `reverse-gemini/tests/test_storage.c`

**Interfaces:**
- Produces:
  - `TTPHeader`, `TTPStep` struct definitions in `ttp_core.h`.
  - `BOOL ttp_save_project(const char* filepath, const TTPStep* steps, DWORD stepCount, const BYTE** bmpBuffers, const DWORD* bmpSizes);`
  - `BOOL ttp_load_project(const char* filepath, TTPStep** outSteps, DWORD* outStepCount, BYTE*** outBmpBuffers, DWORD** outBmpSizes);`
  - `void ttp_free_project(TTPStep* steps, BYTE** bmpBuffers, DWORD* bmpSizes, DWORD stepCount);`

- [ ] **Step 1: Write test for `.ttp` serialization and deserialization**

Create `reverse-gemini/tests/test_storage.c`:
```c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <windows.h>
#include "../src/ttp_core.h"
#include "../src/ttp_storage.h"

int main() {
    TTPStep steps[2];
    memset(steps, 0, sizeof(steps));

    steps[0].stepId = 1;
    steps[0].actionType = TTP_ACTION_CLICK;
    steps[0].targetMode = TTP_TARGET_TEXT;
    steps[0].origX = 100; steps[0].origY = 200;
    steps[0].timeoutMs = 3000;
    steps[0].postDelayMs = 150;
    strcpy(steps[0].textKey, "Submit");

    steps[1].stepId = 2;
    steps[1].actionType = TTP_ACTION_DRAG;
    steps[1].targetMode = TTP_TARGET_COORD;
    steps[1].origX = 300; steps[1].origY = 400;
    steps[1].destX = 500; steps[1].destY = 600;
    steps[1].timeoutMs = 5000;
    steps[1].postDelayMs = 200;

    const char* dummyBmp1 = "BM_DUMMY_BMP_DATA_1";
    const char* dummyBmp2 = "BM_DUMMY_BMP_DATA_2_LONGER";
    const BYTE* bmpBufs[2] = { (const BYTE*)dummyBmp1, (const BYTE*)dummyBmp2 };
    DWORD bmpSizes[2] = { (DWORD)strlen(dummyBmp1), (DWORD)strlen(dummyBmp2) };

    const char* testFile = "test_output.ttp";
    BOOL saved = ttp_save_project(testFile, steps, 2, bmpBufs, bmpSizes);
    assert(saved == TRUE);

    TTPStep* loadedSteps = NULL;
    DWORD loadedCount = 0;
    BYTE** loadedBmps = NULL;
    DWORD* loadedSizes = NULL;

    BOOL loaded = ttp_load_project(testFile, &loadedSteps, &loadedCount, &loadedBmps, &loadedSizes);
    assert(loaded == TRUE);
    assert(loadedCount == 2);
    assert(loadedSteps[0].actionType == TTP_ACTION_CLICK);
    assert(strcmp(loadedSteps[0].textKey, "Submit") == 0);
    assert(loadedSizes[0] == strlen(dummyBmp1));
    assert(memcmp(loadedBmps[0], dummyBmp1, loadedSizes[0]) == 0);
    assert(loadedSteps[1].timeoutMs == 5000);

    ttp_free_project(loadedSteps, loadedBmps, loadedSizes, loadedCount);
    remove(testFile);
    printf("Test Task 1 Passed successfully!\n");
    return 0;
}
```

- [ ] **Step 2: Run test to verify it fails to compile**

Run: `gcc -Ireverse-gemini/src reverse-gemini/tests/test_storage.c -o reverse-gemini/tests/test_storage.exe`
Expected: FAIL with missing `ttp_core.h` and `ttp_storage.h`.

- [ ] **Step 3: Implement `ttp_core.h`, `ttp_storage.h` and `ttp_storage.c`**

Create `reverse-gemini/src/ttp_core.h`:
```c
#ifndef TTP_CORE_H
#define TTP_CORE_H

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#define TTP_ACTION_CLICK     1
#define TTP_ACTION_DBLCLICK  2
#define TTP_ACTION_RCLICK    3
#define TTP_ACTION_DRAG      4
#define TTP_ACTION_TYPE_TEXT 5
#define TTP_ACTION_HOTKEY    6

#define TTP_TARGET_TEXT      1
#define TTP_TARGET_IMAGE     2
#define TTP_TARGET_COORD     3

#pragma pack(push, 1)
typedef struct {
    char  magic[4];       /* "TTP1" */
    DWORD version;        /* 1 */
    DWORD stepCount;      /* N */
    DWORD flags;          /* 0 */
} TTPHeader;

typedef struct {
    DWORD stepId;         /* 1, 2, ... */
    DWORD actionType;     /* TTP_ACTION_* */
    DWORD targetMode;     /* TTP_TARGET_* */
    LONG  origX;          /* Recorded X */
    LONG  origY;          /* Recorded Y */
    LONG  destX;          /* Drag end X */
    LONG  destY;          /* Drag end Y */
    DWORD timeoutMs;      /* Search timeout (default 3000ms) */
    DWORD postDelayMs;    /* Delay after execution (default 100ms) */
    char  textKey[128];   /* Matched text or string to type */
    DWORD imageOffset;    /* Offset to bitmap in asset blob */
    DWORD imageSize;      /* Size of bitmap in bytes */
} TTPStep;
#pragma pack(pop)

#endif
```

Create `reverse-gemini/src/ttp_storage.h`:
```c
#ifndef TTP_STORAGE_H
#define TTP_STORAGE_H

#include "ttp_core.h"

BOOL ttp_save_project(const char* filepath, const TTPStep* steps, DWORD stepCount, const BYTE** bmpBuffers, const DWORD* bmpSizes);
BOOL ttp_load_project(const char* filepath, TTPStep** outSteps, DWORD* outStepCount, BYTE*** outBmpBuffers, DWORD** outBmpSizes);
void ttp_free_project(TTPStep* steps, BYTE** bmpBuffers, DWORD* bmpSizes, DWORD stepCount);

#endif
```

Implement `reverse-gemini/src/ttp_storage.c` with file I/O packing header, step descriptors, and binary image blob consecutively.

- [ ] **Step 4: Compile and run test to verify it passes**

Run: `gcc -Ireverse-gemini/src reverse-gemini/src/ttp_storage.c reverse-gemini/tests/test_storage.c -o reverse-gemini/tests/test_storage.exe; ./reverse-gemini/tests/test_storage.exe`
Expected: Output `Test Task 1 Passed successfully!`.

- [ ] **Step 5: Commit Task 1**

```bash
git add reverse-gemini/src/ttp_core.h reverse-gemini/src/ttp_storage.h reverse-gemini/src/ttp_storage.c reverse-gemini/tests/test_storage.c
git commit -m "feat(pro): implement core data structures and .ttp storage module"
```

---

### Task 2: Pure-C Adaptive Vision & Spatial Disambiguation Engine

**Files:**
- Create: `reverse-gemini/src/ttp_vision.h`
- Create: `reverse-gemini/src/ttp_vision.c`
- Test: `reverse-gemini/tests/test_vision.c`

**Interfaces:**
- Produces:
  - `double ttp_calc_euclidean_dist(LONG x1, LONG y1, LONG x2, LONG y2);`
  - `int ttp_pick_nearest_candidate(LONG origX, LONG origY, const POINT* candidates, int count);`
  - `BOOL ttp_adaptive_crop_button(HDC hdcSrc, LONG clickX, LONG clickY, RECT* outRect, BYTE** outBmp, DWORD* outBmpSize);`
  - `BOOL ttp_match_template_ncc(HDC hdcScreen, const BYTE* bmpPattern, DWORD bmpSize, double minScore, POINT* outMatchPos);`

- [ ] **Step 1: Write test for Euclidean distance, nearest candidate selection and Sobel edge detection**

Create `reverse-gemini/tests/test_vision.c`:
```c
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <math.h>
#include <windows.h>
#include "../src/ttp_vision.h"

int main() {
    // 1. Distance & nearest candidate selection
    POINT candidates[3] = { {100, 100}, {300, 300}, {105, 108} };
    int best = ttp_pick_nearest_candidate(100, 102, candidates, 3);
    assert(best == 0); // (100,100) is closest to (100,102) with dist=2

    int best2 = ttp_pick_nearest_candidate(106, 107, candidates, 3);
    assert(best2 == 2); // (105, 108) is closest to (106, 107)

    printf("Test Task 2 Vision Math Passed successfully!\n");
    return 0;
}
```

- [ ] **Step 2: Run test to verify failure**

Run: `gcc -Ireverse-gemini/src reverse-gemini/tests/test_vision.c -o reverse-gemini/tests/test_vision.exe`
Expected: FAIL with unresolved `ttp_vision.h`.

- [ ] **Step 3: Implement `ttp_vision.h` and `ttp_vision.c`**

Create `reverse-gemini/src/ttp_vision.h` and `reverse-gemini/src/ttp_vision.c`:
- Implement Euclidean distance calculation `sqrt((x1-x2)^2 + (y1-y2)^2)`.
- Implement `ttp_pick_nearest_candidate` selecting minimum distance index.
- Implement `ttp_adaptive_crop_button`: captures 256×256 ROI around `(clickX, clickY)`, computes 3×3 Sobel horizontal and vertical gradients, casts rays in 4 directions to detect prominent boundary contrast, calculates rectangular bounding box, and creates a 24bpp DIB bitmap stream.
- Implement `ttp_match_template_ncc`: converts target DC and template to grayscale, evaluates normalized cross-correlation across sliding window, and returns top coordinate if score $\ge minScore$.

- [ ] **Step 4: Run test to verify it passes**

Run: `gcc -Ireverse-gemini/src reverse-gemini/src/ttp_vision.c reverse-gemini/tests/test_vision.c -lm -lgdi32 -o reverse-gemini/tests/test_vision.exe; ./reverse-gemini/tests/test_vision.exe`
Expected: Output `Test Task 2 Vision Math Passed successfully!`.

- [ ] **Step 5: Commit Task 2**

```bash
git add reverse-gemini/src/ttp_vision.h reverse-gemini/src/ttp_vision.c reverse-gemini/tests/test_vision.c
git commit -m "feat(pro): implement pure-C vision engine and spatial disambiguation"
```

---

### Task 3: Action Synthesizer & Playback Engine

**Files:**
- Create: `reverse-gemini/src/ttp_engine.h`
- Create: `reverse-gemini/src/ttp_engine.c`
- Test: `reverse-gemini/tests/test_engine.c`

**Interfaces:**
- Produces:
  - `void ttp_synth_init(void);`
  - `void ttp_synth_add_mouse_event(DWORD uMsg, LONG x, LONG y, DWORD timestamp);`
  - `void ttp_synth_add_key_event(DWORD vkCode, BOOL isDown, DWORD timestamp);`
  - `DWORD ttp_synth_finalize(TTPStep* outSteps, DWORD maxSteps);`
  - `BOOL ttp_playback_step(const TTPStep* step, const BYTE* bmpData, DWORD bmpSize, HWND hParentForModal);`

- [ ] **Step 1: Write test for Action Synthesizer**

Create `reverse-gemini/tests/test_engine.c`:
- Feed sequence of raw events: MouseDown at (100, 200), MouseMove to (101, 200), MouseUp at (100, 200).
- Verify synthesizer generates 1 `TTP_ACTION_CLICK` step at (100, 200) instead of raw move stream.
- Feed MouseDown at (50, 50), MouseMove to (200, 200), MouseUp at (200, 200).
- Verify synthesizer generates 1 `TTP_ACTION_DRAG` step from (50, 50) to (200, 200).

- [ ] **Step 2: Run test to verify failure**

Run: `gcc -Ireverse-gemini/src reverse-gemini/tests/test_engine.c -o reverse-gemini/tests/test_engine.exe`
Expected: FAIL with missing headers.

- [ ] **Step 3: Implement `ttp_engine.h` and `ttp_engine.c`**

- Build state machine for aggregating raw 10ms sampling into high-level actions.
- Implement element search loop with 200ms polling up to `step->timeoutMs`.
- Implement `ttp_playback_step` with fallback modal dialog supporting `[Retry]`, `[Use Recorded Pos]`, `[Skip]`, and `[Stop]`.

- [ ] **Step 4: Run test to verify synthesizer passes**

Run: `gcc -Ireverse-gemini/src reverse-gemini/src/ttp_engine.c reverse-gemini/src/ttp_vision.c reverse-gemini/tests/test_engine.c -lgdi32 -luser32 -o reverse-gemini/tests/test_engine.exe; ./reverse-gemini/tests/test_engine.exe`
Expected: PASS.

- [ ] **Step 5: Commit Task 3**

```bash
git add reverse-gemini/src/ttp_engine.h reverse-gemini/src/ttp_engine.c reverse-gemini/tests/test_engine.c
git commit -m "feat(pro): implement action synthesizer and playback execution engine"
```

---

### Task 4: Collapsible Drawer UI & TinyTask Pro Shell

**Files:**
- Create: `reverse-gemini/src/tinytask_pro.rc`
- Create: `reverse-gemini/src/tinytask_pro.c`

**Interfaces:**
- Produces:
  - Complete standalone `tinytask_pro.exe` with ribbon toolbar and collapsible drawer.
  - Controls: `SysListView32` showing Step ID, Action, Target/Text, Timeout(s), Asset thumbnail.
  - In-place timeout edit box and Quick action bar (`[Add] [Delete] [Move Up] [Move Down] [Step Run]`).

- [ ] **Step 1: Create resource script `tinytask_pro.rc`**
Reuse TinyTask icon and bitmap, add English menus, accelerator hotkeys, and version info.

- [ ] **Step 2: Implement `tinytask_pro.c`**
- Register `TinyTaskProClass`.
- Build main toolbar (Buttons: Open, Save, Rec, Play, Steps, Options).
- Create child `SysListView32` control and quick action buttons hidden in folded state (height 32px / window height 54px).
- Handle `[Steps]` click: resize window via `SetWindowPos` to `380 × 360 px` and toggle visibility of drawer controls.
- Connect Record/Play buttons to `ttp_engine` and Open/Save to `ttp_storage` (`.ttp` files).
- Handle ListView double-click on Timeout column for inline edit.

- [ ] **Step 3: Compile `tinytask_pro.exe`**

Run: `windres reverse-gemini/src/tinytask_pro.rc -O coff -o reverse-gemini/src/tinytask_pro_res.o; gcc -Os -s -mwindows reverse-gemini/src/tinytask_pro.c reverse-gemini/src/ttp_storage.c reverse-gemini/src/ttp_vision.c reverse-gemini/src/ttp_engine.c reverse-gemini/src/tinytask_pro_res.o -lcomctl32 -loleacc -lgdi32 -luser32 -lcomdlg32 -lshell32 -o reverse-gemini/bin/tinytask_pro.exe`
Expected: Clean build with exit code 0.

- [ ] **Step 4: Commit Task 4**

```bash
git add reverse-gemini/src/tinytask_pro.rc reverse-gemini/src/tinytask_pro.c
git commit -m "feat(pro): implement collapsible drawer UI and TinyTask Pro application shell"
```

---

### Task 5: End-to-End Size Gate & Verification

**Files:**
- Modify: `reverse-gemini/work/tinytask-re/report/tinytask_reverse_engineering_report.md` (append Pro delivery section)

- [ ] **Step 1: Size budget verification**

Run: `Get-Item reverse-gemini/bin/tinytask_pro.exe | Select-Object FullName, Length`
Verification gate: `Length < 800000` (Bytes). Ensure binary is $< 800 \text{ KB}$.

- [ ] **Step 2: Functional verification of executable**
- Launch `bin/tinytask_pro.exe` in test mode / verify clean startup, toolbar rendering, drawer expansion, and graceful exit.
- Test round-trip recording, `.ttp` save, and loading.

- [ ] **Step 3: Update documentation and final commit**

```bash
git add reverse-gemini/bin/tinytask_pro.exe reverse-gemini/work/tinytask-re/report/tinytask_reverse_engineering_report.md
git commit -m "chore(pro): verify binary size budget and finalize TinyTask Pro delivery"
```
