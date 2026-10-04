# Technical Design Specification: Timeout Action UI, Hierarchical Cascaded Matching & Bilingual Documentation

**Date**: 2026-10-04  
**Status**: Approved (Brainstorming Phase)  
**Author**: Antigravity  
**Target Repository**: `reverse-gemini`  

---

## 1. Overview & Problem Statement

TinyTask Pro delivers an ultra-compact (< 100 KB), standalone RPA tool that bridges single-binary recording with semantic actions, vision template matching, and accessible UI tree navigation. Recent user feedback has identified three key evolution requirements:

1. **Per-Step Timeout Fallback Action UI Configuration**:
   - *Current Situation*: The playback engine supports five fallback behaviors (`[Prompt]`, `[Retry]`, `[Coord]`, `[Skip]`, `[Stop]`) encoded in the high 16 bits of `step.targetMode`. However, this was hidden behind typing text strings into the `Timeout(s)` column (e.g. `"3.0 skip"`).
   - *Need*: Dedicated, highly discoverable column and in-place popup context menu to configure the fallback action per step with zero typing friction.
2. **Image Template Matching Latency Optimization (Hierarchical Cascaded Matching)**:
   - *Current Situation*: The playback engine performs full-screen capturing and processing (2560×1440, ~3.68M pixels, ~50MB working allocations) on every polling tick, resulting in 30~45ms per check.
   - *Need*: A two-tier cascaded matching pipeline where 90%+ cases (in-place or small displacement) hit within **1~2ms** via a localized ROI (±200px), with transparent fallback to full-screen pyramid matching only if the localized search misses.
3. **Bilingual Documentation (`reverse-gemini/README.md`)**:
   - Provide complete, professional, comprehensive documentation in Chinese (Part 1) and English (Part 2), covering features, architecture, keyboard shortcuts, UI drawer operations, building, and format specifications.

---

## 2. Architecture & Detailed Specifications

### 2.1 Module 1: Drawer ListView 6-Column Layout & Timeout Menu System

#### 2.1.1 Column Layout Refactor
The drawer ListView (`g_hListView`, ID `ID_LV_STEPS`) in `tinytask_pro.c` will expand from 5 columns to 6 columns, proportionally allocated to fit the 380px expanded window width without horizontal scrollbars:

| Col # | Header Text | Width (px) | SubItem Index | Content / Formatting |
|---|---|:---:|:---:|---|
| 0 | `#` | 28 | 0 | 1-based step index (`1`, `2`, ...) |
| 1 | `Action` | 52 | 1 | `Click`, `DblClick`, `RClick`, `Drag`, `Text`, `Hotkey` |
| 2 | `Target` | 95 | 2 | Accessible control name or typed string |
| 3 | `Timeout` | 50 | 3 | Pure numeric timeout formatted with 1 decimal place (e.g., `3.0s`) |
| 4 | `On Timeout` | 75 | 4 | Policy label: `[Prompt]`, `[Retry]`, `[Coord]`, `[Skip]`, `[Stop]` |
| 5 | `Asset` | 40 | 5 | Indicator: `[BMP]` if image pattern exists, else `-` |

#### 2.1.2 In-Place Timeout Editing & Action Popup Menu
In `WndProc` under `WM_NOTIFY` -> `NM_DBLCLK`:
1. **SubItem 3 (`Timeout`)**:
   - Clicking/double-clicking this column continues to launch `StartInPlaceTimeoutEdit(item)`.
   - The edit box only manages pure seconds (e.g., user types `5.0`). On Enter/KillFocus, it updates `g_steps[item].timeoutMs = (DWORD)(val * 1000.0)`.
2. **SubItem 4 (`On Timeout`)**:
   - Double-clicking SubItem 4 triggers an in-place Win32 popup menu via `TrackPopupMenu`:
     - `Menu Item 1`: `[Prompt] Ask User (Default Dialog)` -> `TTP_TIMEOUT_ACT_DEFAULT` (0)
     - `Menu Item 2`: `[Retry] Retry Until Found` -> `TTP_TIMEOUT_ACT_RETRY` (1)
     - `Menu Item 3`: `[Coord] Click Recorded Position` -> `TTP_TIMEOUT_ACT_USE_RECORDED` (2)
     - `Menu Item 4`: `[Skip] Skip This Step` -> `TTP_TIMEOUT_ACT_SKIP` (3)
     - `Menu Item 5`: `[Stop] Stop Playback` -> `TTP_TIMEOUT_ACT_STOP` (4)
   - When a selection is made, `g_steps[item].targetMode` is updated using `TTP_MAKE_TARGET_MODE(baseMode, chosenAction)`.
   - The cell text is immediately refreshed and `ListView_SetItemText` updates the view.

#### 2.1.3 Storage & Binary Backward Compatibility
- In `ttp_core.h`:
  - `TTP_GET_BASE_TARGET_MODE(step->targetMode)` extracts lower 16 bits.
  - `TTP_GET_TIMEOUT_ACTION(step->targetMode)` extracts upper 16 bits.
  - `TTP_MAKE_TARGET_MODE(base, act)` combines them.
- Existing `.ttp` project files continue to deserialize cleanly with 100% backward and forward binary compatibility.

---

### 2.2 Module 2: Hierarchical Cascaded Vision Matching (Option A)

#### 2.2.1 Cascaded Matching Pipeline
In `reverse-gemini/src/ttp_engine.c` inside `ttp_playback_step()` for both `TTP_TARGET_IMAGE` and dual-engine fallback on `TTP_TARGET_TEXT`:

```
   ┌────────────────────────────────────────────────────────────────┐
   │               Playback Loop Polling for Target                 │
   └────────────────────────────────┬───────────────────────────────┘
                                    │
                                    ▼
       ┌────────────────────────────────────────────────────────┐
       │ Tier 1: Localized Fast ROI Search                      │
       │ Center: (step->origX, step->origY), Radius: 200px      │
       │ Size: 400x400 px, Call: ttp_match_template_ncc_roi()    │
       │ Latency: 1 ~ 2 ms (calculates ~4.3% of full screen)    │
       └────────────────────────────┬───────────────────────────┘
                                    │
                     Matched? (score >= 0.75)
                     ┌──────────────┴──────────────┐
                     │ YES                         │ NO
                     ▼                             ▼
        ┌──────────────────────────┐  ┌───────────────────────────────────┐
        │ Return matchPos (X, Y)   │  │ Tier 2: Full-Screen Pyramid Fallback │
        │ Instant Click Execution  │  │ Call: ttp_match_template_ncc()   │
        │ Latency: < 3 ms          │  │ 2x Anti-aliased Box Pyramid        │
        └──────────────────────────┘  │ Latency: 30 ~ 40 ms               │
                                      └─────────────────┬─────────────────┘
                                                        │
                                         Matched? (score >= 0.75)
                                         ┌──────────────┴──────────────┐
                                         │ YES                         │ NO
                                         ▼                             ▼
                            ┌──────────────────────────┐  ┌──────────────────────────┐
                            │ Return matchPos (X, Y)   │  │ Check Timeout Duration   │
                            │ Execute Target Click     │  │ Expired -> Trigger       │
                            └──────────────────────────┘  │ On Timeout Action Policy │
                                                          └──────────────────────────┘
```

#### 2.2.2 Clamping & Boundary Handling
- When `origX < 200` or `origY < 200` or `origX > screenW - 200` or `origY > screenH - 200`, the ROI bounding box `[x0, y0, x1, y1]` automatically clamps to `[0, 0, screenW, screenH]`.
- Screen DC handles and GDI resources are safely released in all code paths to guarantee 0 GDI object leakage.

---

### 2.3 Module 3: Bilingual Documentation (`reverse-gemini/README.md`)

`reverse-gemini/README.md` will be rewritten with a clean, structured bilingual presentation:
- **Part 1: 简体中文 (Chinese Version)**
  - 项目简介与核心特性（< 100 KB 单二进制、Power Automate 语义动作、双引擎自适应视觉、防抖动、高 DPI 适配）
  - 界面与功能指南（收缩/展开双模态、6 列抽屉说明、各列双击就地编辑技巧）
  - 全局快捷键指南（默认与自定义设置、防震荡上升沿锁存机制）
  - 超时策略（五种策略详解与应用场景）
  - 单文件工程格式规范（`.ttp` 协议）
  - 源码构建与依赖说明（MinGW GCC、纯原生 Win32）
- **Part 2: English Version**
  - Project Overview & Key Features (< 100 KB single binary, Power Automate parity, Dual-engine vision, Debounce click synthesis, High-DPI support)
  - UI Drawer & Column Guide (6-column ListView, In-place double-click editing)
  - Global Hotkeys & Anti-Jitter Edge Latch
  - Timeout Policies & Fallback Strategies
  - Single-File Project Format Specification (`.ttp`)
  - Build & Development Instructions

---

## 3. Testing & Verification Plan

1. **Automated TDD Test Suite (`reverse-gemini/tests/test_fix_repro.c` & `test_engine.c`)**:
   - Verify 6-column ListView initialization and text formatting.
   - Verify `TTP_MAKE_TARGET_MODE` macro round-trip and enum mappings for all 5 timeout actions.
   - Benchmark Tier-1 ROI localized match vs Tier-2 full-screen match:
     - Tier-1 ROI execution must complete in $< 5\text{ms}$.
     - Tier-2 full-screen fallback must locate displaced target with score $\ge 0.75$.
2. **Binary Size Constraint**:
   - Final `tinytask_pro.exe` binary must remain under 100 KB (currently ~83 KB).
3. **Documentation Verification**:
   - `reverse-gemini/README.md` verified for bilingual parity and completeness.

---

## 4. Spec Self-Review Checklist

- [x] **Placeholder Scan**: No `TODO`, `TBD`, or undefined behaviors.
- [x] **Internal Consistency**: Matches existing `.ttp` binary serialization and Win32 controls.
- [x] **Scope Check**: Tightly bounded across UI column refactor, cascaded vision matching, and bilingual documentation.
- [x] **Ambiguity Check**: Subitem indexes, column widths, popup menu IDs, and matching fallbacks are explicitly stated.
