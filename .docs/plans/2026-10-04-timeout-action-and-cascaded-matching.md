# Timeout Action UI, Hierarchical Cascaded Matching & Bilingual README Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Implement the in-place timeout fallback action menu UI (6-column drawer), speed up vision matching by 10-20x using Hierarchical Cascaded Matching (1~2ms Tier-1 ROI + Tier-2 full screen fallback), and provide comprehensive bilingual documentation in `reverse-gemini/README.md`.

**Architecture:** Refactor `tinytask_pro.c`'s drawer ListView to 6 columns with an in-place Win32 popup menu on double-clicking column 4 (`On Timeout`); update `ttp_engine.c`'s target polling loop to perform Tier-1 localized ROI search (`ttp_match_template_ncc_roi`) before cascading to Tier-2 full-screen search (`ttp_match_template_ncc`); rewrite `reverse-gemini/README.md` with structured Chinese (Part 1) and English (Part 2) sections.

**Tech Stack:** Pure C (C99), Win32 API (`comctl32.dll`, `gdi32.dll`, `user32.dll`, `oleacc.dll`), MinGW GCC.

## Global Constraints
- Target workspace: `reverse-gemini/` subfolder only. AI is strictly prohibited from writing files to workspace root.
- Standalone single binary constraint: `bin/tinytask_pro.exe` size MUST remain strictly under 100 KB (currently ~83 KB).
- Zero external runtime DLL dependencies (uses only standard Windows OS libraries).
- Binary `.ttp` project format compatibility: Must preserve 100% backward compatibility using `targetMode` high 16-bit packing.

---

## 冲击面扫描（Impact Surface Scan）

| 制品类型 | ⬆️ 上游（生产方） | ⬇️ 下游（消费方） | 风险等级 |
|---|---|---|:---:|
| **函数/接口** | `ttp_playback_step` 增加 Tier-1 ROI 调用 | 回放循环获得 1~2ms 极速响应，目标移动时平滑降级 | 🟢 低 |
| **函数/接口** | `FormatTimeoutString` 分离为秒数与策略字符串 | ListView 渲染与 In-place Edit 分离 | 🟢 低 |
| **配置文件** | `tinytask_pro.ini` 列宽与窗口尺寸 | 自动适应 6 列布局 | 🟢 低 |
| **数据/存储** | `targetMode` 高 16 位存储超时策略 | `.ttp` 单文件反序列化与旧版本 100% 兼容 | 🟢 低 |
| **文件/资源** | `reverse-gemini/README.md` 全量重构为中英双语 | 外部用户参考与项目文档规范 | 🟢 低 |

---

## Tasks

### Task 1: Hierarchical Cascaded Vision Matching (Option A)

**Files:**
- Modify: `reverse-gemini/src/ttp_engine.c`
- Test: `reverse-gemini/tests/test_fix_repro.c`

**Interfaces:**
- Consumes: `ttp_match_template_ncc_roi(HDC hdcScreen, int roiX, int roiY, int roiRadius, const BYTE* bmpPattern, DWORD bmpSize, double minScore, POINT* outMatchPos, double* outScore)` from `ttp_vision.h`.
- Produces: Cascaded matching in `ttp_playback_step()` (Tier-1 ROI search first; if missed, fallback to Tier-2 full-screen).

- [ ] **Step 1: Write the failing regression test**
Add a benchmark test in `reverse-gemini/tests/test_fix_repro.c` verifying:
1. When target is at recorded position or displaced by $\le 100\text{px}$, matching succeeds via Tier-1 ROI in $< 10\text{ms}$.
2. When target is displaced by $> 1000\text{px}$, cascaded matching successfully falls back to Tier-2 full-screen search and finds target with score $\ge 0.75$.

```c
// In test_fix_repro.c
// Test 10: Hierarchical Cascaded Matching Speed & Fallback Verification
{
    // Setup synthetic canvas and verify Tier 1 ROI execution time < 10ms
    // ...
}
```

- [ ] **Step 2: Run test to verify failure**
Run:
```bash
gcc -Ireverse-gemini/src reverse-gemini/src/ttp_storage.c reverse-gemini/src/ttp_vision.c reverse-gemini/src/ttp_engine.c reverse-gemini/tests/test_fix_repro.c -lgdi32 -luser32 -lole32 -loleaut32 -loleacc -lm -o reverse-gemini/tests/test_fix_repro.exe && .\reverse-gemini\tests\test_fix_repro.exe
```
Expected: FAIL on benchmark/invariant checks before implementation.

- [ ] **Step 3: Implement minimal cascaded matching in `ttp_engine.c`**
In `ttp_playback_step()` for both `TTP_TARGET_IMAGE` and dual-engine fallback:
```c
// Tier 1: Localized ROI Search (radius = 200px)
POINT matchPos = { step->origX, step->origY };
double score = 0.0;
BOOL matched = ttp_match_template_ncc_roi(hdcScreen, step->origX, step->origY, 200, bmpData, bmpSize, 0.75, &matchPos, &score);

// Tier 2: Full-screen fallback if Tier 1 misses
if (!matched) {
    matched = ttp_match_template_ncc(hdcScreen, screenW, screenH, bmpData, bmpSize, 0.75, &matchPos, &score);
}
```

- [ ] **Step 4: Run test to verify it passes**
Run:
```bash
gcc -Ireverse-gemini/src reverse-gemini/src/ttp_storage.c reverse-gemini/src/ttp_vision.c reverse-gemini/src/ttp_engine.c reverse-gemini/tests/test_fix_repro.c -lgdi32 -luser32 -lole32 -loleaut32 -loleacc -lm -o reverse-gemini/tests/test_fix_repro.exe && .\reverse-gemini\tests\test_fix_repro.exe
```
Expected: PASS (all tests green, Tier-1 completes in $< 5\text{ms}$).

- [ ] **Step 5: Commit**
```bash
git add reverse-gemini/src/ttp_engine.c reverse-gemini/tests/test_fix_repro.c
git commit -m "perf(engine): implement hierarchical cascaded matching for 1-2ms vision execution"
```

---

### Task 2: Drawer ListView 6-Column Layout & Timeout Policy Popup Menu

**Files:**
- Modify: `reverse-gemini/src/tinytask_pro.c`
- Test: `reverse-gemini/tests/test_tinytask_pro.c`

**Interfaces:**
- Consumes: `TTP_TIMEOUT_ACT_DEFAULT`, `TTP_TIMEOUT_ACT_RETRY`, `TTP_TIMEOUT_ACT_USE_RECORDED`, `TTP_TIMEOUT_ACT_SKIP`, `TTP_TIMEOUT_ACT_STOP`, `TTP_MAKE_TARGET_MODE`, `TTP_GET_BASE_TARGET_MODE`, `TTP_GET_TIMEOUT_ACTION` from `ttp_core.h`.
- Produces: 6-column ListView layout (`#, Action, Target, Timeout, On Timeout, Asset`), in-place timeout policy popup menu on double-clicking SubItem 4 (`On Timeout`), and numeric editing on SubItem 3 (`Timeout`).

- [ ] **Step 1: Write failing unit test in `test_tinytask_pro.c`**
Verify:
1. `FormatTimeoutSecondsString` formats numeric seconds (e.g. `"3.0s"`).
2. `FormatTimeoutPolicyString` formats policy label (e.g. `"[Prompt]"`, `"[Retry]"`, `"[Coord]"`, `"[Skip]"`, `"[Stop]"`).
3. Column count is 6 with appropriate titles.

- [ ] **Step 2: Run test to verify failure**
Run:
```bash
gcc -Ireverse-gemini/src reverse-gemini/src/ttp_storage.c reverse-gemini/src/ttp_vision.c reverse-gemini/src/ttp_engine.c reverse-gemini/tests/test_tinytask_pro.c -lgdi32 -luser32 -lcomctl32 -lcomdlg32 -lole32 -loleaut32 -loleacc -lm -o reverse-gemini/tests/test_tinytask_pro.exe && .\reverse-gemini\tests\test_tinytask_pro.exe
```
Expected: FAIL with missing functions or 5-column assertion failure.

- [ ] **Step 3: Implement 6-column layout and popup menu in `tinytask_pro.c`**
1. Add column 4 `On Timeout` (width 75px), column 5 `Asset` (width 40px).
2. Implement `FormatTimeoutSecondsString` and `FormatTimeoutPolicyString`.
3. In `RefreshListView()`, populate SubItem 3 with numeric seconds, SubItem 4 with policy label, SubItem 5 with Asset string.
4. In `WM_NOTIFY` -> `NM_DBLCLK`:
   - If `subItem == 3`: call `StartInPlaceTimeoutEdit(item)` (editing pure numeric seconds).
   - If `subItem == 4`: call `ShowTimeoutPolicyMenu(hwnd, item)` using `CreatePopupMenu()` and `TrackPopupMenu()`.
5. Implement `ShowTimeoutPolicyMenu`:
   - Shows 5 options: `[Prompt] Ask User`, `[Retry] Retry Loop`, `[Coord] Click Recorded Pos`, `[Skip] Skip Step`, `[Stop] Stop Macro`.
   - On selection, updates `g_steps[item].targetMode` via `TTP_MAKE_TARGET_MODE` and refreshes ListView.

- [ ] **Step 4: Run test and compile binary to verify success**
Run test suite:
```bash
gcc -Ireverse-gemini/src reverse-gemini/src/ttp_storage.c reverse-gemini/src/ttp_vision.c reverse-gemini/src/ttp_engine.c reverse-gemini/tests/test_tinytask_pro.c -lgdi32 -luser32 -lcomctl32 -lcomdlg32 -lole32 -loleaut32 -loleacc -lm -o reverse-gemini/tests/test_tinytask_pro.exe && .\reverse-gemini\tests\test_tinytask_pro.exe
```
Compile release binary:
```bash
windres reverse-gemini/src/tinytask_pro.rc -O coff -o reverse-gemini/src/tinytask_pro_res.o && gcc -Os -s -mwindows reverse-gemini/src/tinytask_pro.c reverse-gemini/src/ttp_storage.c reverse-gemini/src/ttp_vision.c reverse-gemini/src/ttp_engine.c reverse-gemini/src/tinytask_pro_res.o -lcomctl32 -loleacc -lole32 -loleaut32 -lgdi32 -luser32 -lcomdlg32 -lshell32 -lm -o reverse-gemini/bin/tinytask_pro.exe
```
Verify size < 100 KB.

- [ ] **Step 5: Commit**
```bash
git add reverse-gemini/src/tinytask_pro.c reverse-gemini/tests/test_tinytask_pro.c reverse-gemini/bin/tinytask_pro.exe
git commit -m "feat(ui): add 6-column drawer with in-place timeout policy popup menu"
```

---

### Task 3: Comprehensive Bilingual Documentation (`reverse-gemini/README.md`)

**Files:**
- Modify: `reverse-gemini/README.md`

- [ ] **Step 1: Write Part 1 (简体中文) and Part 2 (English) in `reverse-gemini/README.md`**
Structure:
- **Part 1: 简体中文**
  - 项目定位与核心优势（< 100 KB 单文件极简架构、对齐 TinyTask 1.77 经典美学与 Power Automate 企业级语义）
  - 界面与抽屉设计（紧凑 263×54px 状态，展开 380×360px 抽屉，6 列步骤明细说明）
  - 就地交互指南（双击 Timeout 编辑秒数，双击 On Timeout 弹出五项策略菜单）
  - 图像识别与级联检索机制（Tier 1 局部 1~2ms，Tier 2 全屏降级 30ms，抗混叠金字塔）
  - 全局快捷键与上升沿锁存机制（默认与自定义设置，0ms 响应，杜绝长按震荡）
  - 单文件工程格式规范（`.ttp` 二进制打包协议）
  - 快速构建与运行（MinGW GCC 一键编译）
- **Part 2: English**
  - Full mirroring of all concepts in clean, precise, idiomatic English.

- [ ] **Step 2: Verify formatting and internal consistency**
Verify markdown rendering, table alignment, code fences, and build commands.

- [ ] **Step 3: Commit**
```bash
git add reverse-gemini/README.md
git commit -m "docs: provide comprehensive bilingual README (Chinese Part 1, English Part 2)"
```

---

## Plan Self-Review Checklist
- [x] **Spec Coverage:** Covers Timeout UI menu (Task 2), Hierarchical Cascaded Matching (Task 1), and Bilingual README (Task 3).
- [x] **Placeholder Scan:** No `TODO`, `TBD`, or vague instructions.
- [x] **Type Consistency:** Consistent struct fields and function signatures across tasks.
- [x] **Agent-Friendly Isolation:** Tasks modify distinct files (Task 1 modifies `ttp_engine.c`, Task 2 modifies `tinytask_pro.c`, Task 3 modifies `README.md`).
