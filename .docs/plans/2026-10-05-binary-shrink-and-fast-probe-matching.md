# Binary Shrink and Fast Cascaded Template Matching Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Shrink `tinytask_pro.exe` binary size by 40%+ (from ~99.8 KB to $\le 60\text{ KB}$) and overhaul visual template matching into a zero-heap, microsecond-level cascaded engine (Probe Skip Fast SAD + Fixed-Point NCC), executed in two strictly gated phases with manual human approval between them.

**Architecture:** Phase 1 focuses on compiler/linker flags, SEH unwinding table removal, and asset compaction with a mandatory human verification gate. Phase 2 rewrites `ttp_vision.c` into a zero-heap, cache-friendly architecture with Tier 1 sparse probe skipping SAD and Tier 2 fixed-point NCC fallback, maintaining 100% backward compatibility with `ttp_vision.h`.

**Tech Stack:** C (C99/C11), Win32 API, GDI, MinGW-w64 GCC (`-Os -flto -ffunction-sections -fdata-sections -Wl,--gc-sections -fno-asynchronous-unwind-tables -fno-ident`).

## Global Constraints

- Root directory protection: AI writes only into subdirectories (`reverse-gemini/`).
- Preserve original baseline: Do NOT modify `reverse-gemini/src/tinytask.c` or `Library/tinytask.exe`.
- Backward compatibility: Public API signatures in `reverse-gemini/src/ttp_vision.h` must remain 100% untouched.
- Binary size budget: Final `bin/tinytask_pro.exe` must strictly be $\le 60,000$ bytes ($\le 60\text{ KB}$).
- No third-party packers: Pure native PE binary without UPX or packing to prevent AV false positives.
- Phase separation & gate: Phase 1 (Shrink) must be completed and manually verified by the user before starting Phase 2 (Vision Overhaul).

---

## 冲击面扫描 (Impact Surface Analysis)

| 制品类型 | 生产方 | 消费方 | 风险与防护策略 |
|---|---|---|---|
| 函数/接口 (`ttp_vision.h`) | `ttp_vision.c` | `ttp_engine.c`, `tinytask_pro.c` | 保持函数签名与浮点置信度标度 (0.0~1.0) 不变，内部透明转换 |
| 资源位图 (`toolbar.bmp`) | `tinytask_pro.rc` | `tinytask_pro.c` 菜单/工具栏渲染 | 保持相同的位图尺寸与按钮索引，仅优化调色板/色彩深度 |
| 可执行文件 (`tinytask_pro.exe`) | 构建管线 | 用户运行 | `-fno-asynchronous-unwind-tables` 剥离 SEH 表，需回归验证异常处理稳定性 |
| 静态内存缓冲区 | `ttp_vision.c` | 内部搜索例程 | 静态单例缓冲区按需懒加载，确保多线程/重入安全 |

---

# 阶段一：可执行文件极致瘦身 (Phase 1: Binary Size Reduction)

### Task 1: 编译器/链接器参数优化与段剥离

**Files:**
- Modify: `reverse-gemini/.docs/plans/2026-10-04-tinytask-pro-implementation.md` (记录构建参数变更)
- Build artifact: `reverse-gemini/bin/tinytask_pro.exe`

**Interfaces:**
- Consumes: 现存所有 C 源码与资源对象
- Produces: 开启 `-flto -ffunction-sections -fdata-sections -Wl,--gc-sections -fno-asynchronous-unwind-tables -fno-ident` 的原生 PE 可执行文件

- [ ] **Step 1: 验证当前基准体积**

运行：
```powershell
Get-Item reverse-gemini/bin/tinytask_pro.exe | Select-Object FullName, Length
```
记录基准体积为 99,840 字节。

- [ ] **Step 2: 重新编译并应用激进剥离参数**

运行构建命令：
```powershell
windres reverse-gemini/src/tinytask_pro.rc -O coff -o reverse-gemini/src/tinytask_pro_res.o
gcc -Os -flto -ffunction-sections -fdata-sections -Wl,--gc-sections -fno-asynchronous-unwind-tables -fno-ident -mwindows reverse-gemini/src/tinytask_pro.c reverse-gemini/src/ttp_storage.c reverse-gemini/src/ttp_vision.c reverse-gemini/src/ttp_engine.c reverse-gemini/src/tinytask_pro_res.o -lcomctl32 -loleacc -lgdi32 -luser32 -lcomdlg32 -lshell32 -o reverse-gemini/bin/tinytask_pro.exe
```

- [ ] **Step 3: 检查各段大小变化**

运行：
```powershell
objdump -h reverse-gemini/bin/tinytask_pro.exe
Get-Item reverse-gemini/bin/tinytask_pro.exe | Select-Object Length
```
验证 `.pdata` 与 `.xdata` 段已被彻底消除，`.text` 显著缩减。

---

### Task 2: 内嵌资源轻量化 (`.rsrc`)

**Files:**
- Modify: `reverse-gemini/src/toolbar.bmp`
- Modify: `reverse-gemini/src/tinytask.ico`
- Modify: `reverse-gemini/src/tinytask_pro.rc`

**Interfaces:**
- Consumes: Win32 GDI Toolbar 图像
- Produces: 紧凑型调色板位图与精简图标（资源总占用从 21.5 KB 降至 8~10 KB）

- [ ] **Step 1: 检查当前资源体积**

查看原 `toolbar.bmp`（12,870 字节）与 `tinytask.ico`（7,406 字节）。

- [ ] **Step 2: 优化位图色彩深度**

将 24-bit 连续无压缩 RGB 转为 8-bit / 4-bit 调色板 BMP，保持 168x28 像素比例不变，视觉无损。

- [ ] **Step 3: 优化图标尺寸层**

精简多余的大图层，保留 16x16 与 32x32 基础尺寸。

- [ ] **Step 4: 重新编译资源并链接**

```powershell
windres reverse-gemini/src/tinytask_pro.rc -O coff -o reverse-gemini/src/tinytask_pro_res.o
gcc -Os -flto -ffunction-sections -fdata-sections -Wl,--gc-sections -fno-asynchronous-unwind-tables -fno-ident -mwindows reverse-gemini/src/tinytask_pro.c reverse-gemini/src/ttp_storage.c reverse-gemini/src/ttp_vision.c reverse-gemini/src/ttp_engine.c reverse-gemini/src/tinytask_pro_res.o -lcomctl32 -loleacc -lgdi32 -luser32 -lcomdlg32 -lshell32 -o reverse-gemini/bin/tinytask_pro.exe
```

---

### Task 3: 阶段一体积门禁验证与功能回归

**Files:**
- Verify: `reverse-gemini/bin/tinytask_pro.exe`
- Test: `reverse-gemini/tests/test_storage.c`, `reverse-gemini/tests/test_engine.c`, `reverse-gemini/tests/test_tinytask_pro.c`

- [ ] **Step 1: 运行全套单元测试**

```powershell
gcc -Ireverse-gemini/src reverse-gemini/tests/test_storage.c reverse-gemini/src/ttp_storage.c -o reverse-gemini/tests/test_storage.exe; ./reverse-gemini/tests/test_storage.exe
gcc -Ireverse-gemini/src reverse-gemini/src/ttp_engine.c reverse-gemini/src/ttp_vision.c reverse-gemini/tests/test_engine.c -lgdi32 -luser32 -o reverse-gemini/tests/test_engine.exe; ./reverse-gemini/tests/test_engine.exe
gcc -Ireverse-gemini/src reverse-gemini/tests/test_tinytask_pro.c -luser32 -o reverse-gemini/tests/test_tinytask_pro.exe; ./reverse-gemini/tests/test_tinytask_pro.exe
```
确保全绿 PASS。

- [ ] **Step 2: 验证体积缩减门禁**

```powershell
Get-Item reverse-gemini/bin/tinytask_pro.exe | Select-Object FullName, Length
```
验收标准：`Length <= 65000` (从 99.8KB 缩减至 65KB 以内)。

- [ ] **Step 3: 提交阶段一成果到 Git**

```powershell
git add reverse-gemini/src/toolbar.bmp reverse-gemini/src/tinytask.ico reverse-gemini/src/tinytask_pro.rc reverse-gemini/bin/tinytask_pro.exe
git commit -m "perf(shrink): optimize compiler flags, prune sections and compact resources"
```

---

## 🛑 [HARD HUMAN GATE] 第一阶段人工验证停顿点

> **注意：在用户明确确认第一阶段瘦身后功能完全正常之前，严禁启动阶段二！**
> 
> 请用户启动 `reverse-gemini/bin/tinytask_pro.exe` 进行人工点检：
> 1. 窗口与工具栏 6 个图标（Open, Save, Rec, Play, Steps, Options）渲染是否清晰完好。
> 2. 点击 `[Steps]` 折叠抽屉展开与收起是否顺畅。
> 3. 点击 `[Rec]` 与 `[Play]` 基础录制与回放是否正常。
> 4. 用户在会话中回复“确认正常，启动阶段二”后，方可继续执行后续 Task。

---

# 阶段二：微秒级零堆内存分层搜图引擎 (Phase 2: Vision Engine Overhaul)

### Task 4: 零堆内存架构与纯整型定点灰度化

**Files:**
- Modify: `reverse-gemini/src/ttp_vision.c`
- Test: `reverse-gemini/tests/test_vision.c`

**Interfaces:**
- Produces: `ttp_init_screen_buffer()`, 静态复用 8-bit 单通道灰度捕获，消除 `malloc`
- Consumes: GDI HDC

- [ ] **Step 1: 编写零堆分配单元测试**

在 `test_vision.c` 中添加对全屏灰度转换时无动态内存申请的断言。

- [ ] **Step 2: 实现静态复用灰度缓冲区与整型位移灰度化**

在 `ttp_vision.c` 中引入静态 `s_screenGray`，使用 `(R * 77 + G * 150 + B * 29) >> 8` 替代浮点运算。

- [ ] **Step 3: 验证测试通过**

运行测试，确保 1080P 截屏灰度化耗时 $\le 0.5\text{ ms}$。

---

### Task 5: Tier 1 稀疏特征探测跳步与快速 SAD 早停

**Files:**
- Modify: `reverse-gemini/src/ttp_vision.c`
- Test: `reverse-gemini/tests/test_vision.c`

**Interfaces:**
- Produces: `ttp_probe_and_sad_match()` 稀疏跳步探测与 SAD 累加早停
- Consumes: 模板位图与屏幕灰度缓冲区

- [ ] **Step 1: 编写探测点提取与跳步性能测试**

在 `test_vision.c` 中构造典型按钮模板与背景图，测试 8 个探测点提取与快速跳步。

- [ ] **Step 2: 实现 8 点探测提取器与短路跳步滑动循环**

在 `ttp_vision.c` 中实现提取 4 角 + 1 中心 + 3 梯度极值点，并实现主循环中的短路判断：
```c
if (abs(screen[y + dy0][x + dx0] - G0) > TOLERANCE) continue;
...
```

- [ ] **Step 3: 实现 SAD 累积与动态早停**

当累加误差超过 `bestSAD` 时立即 `break`。若平均误差 $\le 8$，直接作为最终命中返回。

- [ ] **Step 4: 验证单帧搜索耗时**

运行测试，验证 1080P 全屏搜索耗时降至 $\le 2.5\text{ ms}$。

---

### Task 6: Tier 2 定点整型 NCC 兜底验证与整数快速开方

**Files:**
- Modify: `reverse-gemini/src/ttp_vision.c`
- Test: `reverse-gemini/tests/test_vision.c`

**Interfaces:**
- Produces: 纯整型 Q15 定点 NCC 算法与 `isqrt()` 快速整数开方
- Consumes: Tier 1 弱候选点列表

- [ ] **Step 1: 编写定点整数开方与 NCC 测试**

在 `test_vision.c` 中测试 `isqrt()` 精确度及 Q15 定点相关度与浮点 NCC 的等价性（误差 $\le 0.01$）。

- [ ] **Step 2: 实现 32 位位移牛顿迭代开方 `isqrt()`**

- [ ] **Step 3: 实现定点化 NCC 局部打分与置信度归一化**

在弱置信度时对候选点周围 $\pm 16$ 像素区域执行定点 NCC，并映射回标准的 $0.0 \sim 1.0$ 标度。

- [ ] **Step 4: 验证所有容错测试项**

确保抗噪测试（$\pm 15$ 灰度偏差）与弱光测试全部通过。

---

### Task 7: 端到端搜图基准测试与最终双重门禁验证

**Files:**
- Build: `reverse-gemini/bin/tinytask_pro.exe`
- Test: `reverse-gemini/tests/test_vision.exe`, `reverse-gemini/tests/test_engine.exe`

- [ ] **Step 1: 运行全屏搜图 100 次基准测试**

验证平均单次耗时 $\le 2.5\text{ ms}$，堆内存分配为 0 字节。

- [ ] **Step 2: 执行全链路回归测试**

运行 `test_storage.exe`、`test_engine.exe`、`test_tinytask_pro.exe`，确保 100% 通过。

- [ ] **Step 3: 编译发布最终二进制并验证最终体积门禁**

```powershell
windres reverse-gemini/src/tinytask_pro.rc -O coff -o reverse-gemini/src/tinytask_pro_res.o
gcc -Os -flto -ffunction-sections -fdata-sections -Wl,--gc-sections -fno-asynchronous-unwind-tables -fno-ident -mwindows reverse-gemini/src/tinytask_pro.c reverse-gemini/src/ttp_storage.c reverse-gemini/src/ttp_vision.c reverse-gemini/src/ttp_engine.c reverse-gemini/src/tinytask_pro_res.o -lcomctl32 -loleacc -lgdi32 -luser32 -lcomdlg32 -lshell32 -o reverse-gemini/bin/tinytask_pro.exe
Get-Item reverse-gemini/bin/tinytask_pro.exe | Select-Object FullName, Length
```
验收门禁：`Length <= 60000` (最终体积 $\le 60\text{ KB}$)。

- [ ] **Step 4: 提交第二阶段成果到 Git**

```powershell
git add reverse-gemini/src/ttp_vision.c reverse-gemini/tests/test_vision.c reverse-gemini/bin/tinytask_pro.exe
git commit -m "feat(vision): implement zero-heap microsecond cascaded probe matching and fixed-point NCC"
```
