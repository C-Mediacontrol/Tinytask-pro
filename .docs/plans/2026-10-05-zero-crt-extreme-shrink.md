# TinyTask Pro: 零 CRT 纯原生 Win32 极限瘦身实施计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 将 TinyTask Pro 重构为彻底剥离 C 标准库（CRT）与 UCRT 依赖的纯原生 Win32 应用，将编译产物二进制体积从当前的 100.8 KB 压缩至 **40 ~ 50 KB** 区间（腰斩 50%~60%），并达成 0 个 `api-ms-win-crt-*` DLL 依赖。

**Architecture:** 全面清除源码中的 `<stdio.h>`、`<stdlib.h>`、`<string.h>`，将内存管理平滑对接 Windows 进程堆（`HeapAlloc` / `HeapFree` / `HeapReAlloc`），将文件 I/O 替换为 Win32 原生句柄 API（`CreateFileA` / `ReadFile` / `WriteFile`），将字符串格式化收敛至 `wsprintfA`，并通过显式自定义入口点 `WinMainCRTStartup` 与 `-nostdlib -mno-stack-arg-probe` 构建链彻底剥离 MinGW 运行时桩与 libgcc 辅助函数。

**Tech Stack:** Pure C99, Win32 Native APIs (`KERNEL32`, `USER32`, `GDI32`, `COMCTL32`, `comdlg32`, `SHELL32`, `OLE32`, `OLEACC`, `OLEAUT32`), MinGW-w64 GCC, `windres`.

## Global Constraints

- **Spec Document:** `reverse-gemini/.docs/specs/2026-10-05-zero-crt-extreme-shrink-design.md`
- **Target Binary Size Gate:** `Length <= 51,200` 字节（严格 $\le \mathbf{50\text{ KB}}$）
- **Zero-CRT Dependency Gate:** 导入表中严禁出现任何 `api-ms-win-crt-*` 或 `msvcrt.dll` 项
- **Feature Parity Gate:** 完整保留抽屉列表、步骤编辑弹窗、60FPS 棋盘格实时预览与掩码分层搜图引擎，逻辑零删减
- **Directory Constraint:** 严禁在工作区根目录写入任何文件，所有代码与文档必须置于 `reverse-gemini/` 下

---

### Task 1: `ttp_storage.c` 纯 Win32 原生文件 I/O 与内存重构

**Files:**
- Modify: `reverse-gemini/src/ttp_storage.c:1-320`
- Test: `reverse-gemini/tests/test_storage.c`

**Interfaces:**
- Consumes: `reverse-gemini/src/ttp_core.h`, Win32 API (`CreateFileA`, `ReadFile`, `WriteFile`, `SetFilePointer`, `CloseHandle`, `HeapAlloc`, `HeapFree`, `GetProcessHeap`)
- Produces: 
  - `BOOL ttp_save_project(const char* path, const TTPStep* steps, BYTE* const* bmpBuffers, const DWORD* bmpSizes, DWORD count)`
  - `BOOL ttp_load_project(const char* path, TTPStep** outSteps, BYTE*** outBmpBuffers, DWORD** outBmpSizes, DWORD* outCount)`
  - `void ttp_free_project(TTPStep* steps, BYTE** bmpBuffers, DWORD* bmpSizes, DWORD count)`

- [ ] **Step 1: 审查当前 `test_storage.c` 测试行为**

运行当前测试确保基线为绿灯：
```powershell
gcc -Os -Ireverse-gemini/src reverse-gemini/tests/test_storage.c reverse-gemini/src/ttp_storage.c -o reverse-gemini/tests/test_storage.exe
.\reverse-gemini\tests\test_storage.exe
```
预期：PASS。

- [ ] **Step 2: 改造 `src/ttp_storage.c` 移除 CRT 依赖**

1. 移除 `#include <stdio.h>`、`<stdlib.h>`、`<string.h>`；
2. 引入 `#define WIN32_LEAN_AND_MEAN` 与 `#include <windows.h>`；
3. 将所有 `malloc(sz)` 替换为 `(BYTE*)HeapAlloc(GetProcessHeap(), 0, (sz))`；
4. 将所有 `calloc(n, sz)` 替换为 `HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, (size_t)(n) * (size_t)(sz))`；
5. 将所有 `free(p)` 替换为 `if (p) { HeapFree(GetProcessHeap(), 0, p); }`；
6. 将 `fopen(path, "rb")` 改为 `CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL)`；
7. 将 `fopen(path, "wb")` 改为 `CreateFileA(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL)`；
8. 将 `fread(buf, 1, sz, fp)` 改为调用 `ReadFile(hFile, buf, sz, &dwRead, NULL)` 并校验 `dwRead == sz`；
9. 将 `fwrite(buf, 1, sz, fp)` 改为调用 `WriteFile(hFile, buf, sz, &dwWritten, NULL)` 并校验 `dwWritten == sz`；
10. 将 `fseek(fp, offset, SEEK_SET)` 改为 `SetFilePointer(hFile, (LONG)offset, NULL, FILE_BEGIN)`；
11. 将 `fclose(fp)` 改为 `CloseHandle(hFile)`；
12. 将 `memcpy` 与 `memset` 替换为 `__builtin_memcpy` 与 `__builtin_memset`。

- [ ] **Step 3: 运行 `test_storage.exe` 验证功能回归**

```powershell
gcc -Os -Ireverse-gemini/src reverse-gemini/tests/test_storage.c reverse-gemini/src/ttp_storage.c -o reverse-gemini/tests/test_storage.exe
.\reverse-gemini\tests\test_storage.exe
```
预期：PASS（7/7 存取、向后兼容 24bpp 提升及容差持久化全部通过）。

- [ ] **Step 4: 提交 Task 1**

```bash
git add reverse-gemini/src/ttp_storage.c
git commit -m "refactor(storage): migrate file IO and heap management to pure Win32 native APIs"
```

---

### Task 2: `ttp_vision.c` 纯 Win32 堆内存与内联指令改造

**Files:**
- Modify: `reverse-gemini/src/ttp_vision.c:1-1350`
- Test: `reverse-gemini/tests/test_chromakey.c`, `reverse-gemini/tests/test_masked_vision.c`

**Interfaces:**
- Consumes: Win32 GDI/User32/OleAcc, `__builtin_abs`, `__builtin_memcpy`, `__builtin_memset`, `HeapAlloc`, `HeapFree`
- Produces:
  - `BOOL ttp_chromakey_mask(const BYTE* srcBmp, DWORD srcSize, BYTE tol, BYTE** outBmp, DWORD* outSize, float* outRatio)`
  - `BYTE* ttp_adaptive_crop_button(HWND hTargetWnd, POINT ptScreen, DWORD* outSize)`
  - `float ttp_match_template_masked_ncc(HDC hScreenDC, int screenW, int screenH, const BYTE* templBmp, DWORD templSize, POINT* outPt)`

- [ ] **Step 1: 改造 `src/ttp_vision.c` 移除 CRT 依赖**

1. 移除 `#include <stdlib.h>` 与 `#include <string.h>`；
2. 确保包含 `#define WIN32_LEAN_AND_MEAN` 与 `#include <windows.h>`；
3. 将所有 `malloc(sz)` 替换为 `HeapAlloc(GetProcessHeap(), 0, (sz))`；
4. 将所有 `free(ptr)` 替换为 `if (ptr) { HeapFree(GetProcessHeap(), 0, ptr); }`；
5. 将 `abs()` 调用替换为 GCC 内置内联函数 `__builtin_abs()`；
6. 将 `memcpy` / `memset` / `memcmp` 替换为 `__builtin_memcpy` / `__builtin_memset` / `__builtin_memcmp`；
7. 将 `strlen` / `strcpy` 替换为 Windows 原生 `lstrlenA` / `lstrcpynA`；
8. 实现极简内联字符串查找辅助函数（替代 `strstr`）。

- [ ] **Step 2: 编译并运行视觉搜图相关测试套件**

```powershell
gcc -Os -Ireverse-gemini/src reverse-gemini/tests/test_chromakey.c reverse-gemini/src/ttp_storage.c reverse-gemini/src/ttp_vision.c reverse-gemini/src/ttp_engine.c -lole32 -loleaut32 -loleacc -lgdi32 -luser32 -o reverse-gemini/tests/test_chromakey.exe
.\reverse-gemini\tests\test_chromakey.exe

gcc -Os -Ireverse-gemini/src reverse-gemini/tests/test_masked_vision.c reverse-gemini/src/ttp_storage.c reverse-gemini/src/ttp_vision.c reverse-gemini/src/ttp_engine.c -lole32 -loleaut32 -loleacc -lgdi32 -luser32 -o reverse-gemini/tests/test_masked_vision.exe
.\reverse-gemini\tests\test_masked_vision.exe
```
预期：所有测试 100% PASS（右上角变幻壁纸得分 $\ge 0.95$）。

- [ ] **Step 3: 提交 Task 2**

```bash
git add reverse-gemini/src/ttp_vision.c
git commit -m "refactor(vision): migrate memory and math primitives to pure Win32 heap and compiler builtins"
```

---

### Task 3: `ttp_engine.c` 纯 Win32 堆内存、按键缓冲与原生日志重构

**Files:**
- Modify: `reverse-gemini/src/ttp_engine.c:1-850`
- Test: `reverse-gemini/tests/test_engine.c`

**Interfaces:**
- Consumes: Win32 API (`HeapAlloc`, `HeapReAlloc`, `HeapFree`, `CreateFileA`, `WriteFile`, `wsprintfA`)
- Produces:
  - `void ttp_synth_reset(void)`
  - `BOOL ttp_synth_add_mouse_event(DWORD msg, DWORD x, DWORD y, DWORD flags)`
  - `BOOL ttp_synth_add_key_event(DWORD msg, DWORD vk, DWORD scan, DWORD flags)`
  - `void ttp_diag_log(const char* fmt, ...)`
  - `int ttp_playback_step(const TTPStep* step, BYTE* bmpData, DWORD bmpSize, int maxTries, int stepIndex)`

- [ ] **Step 1: 改造 `src/ttp_engine.c` 移除 CRT 依赖**

1. 移除 `#include <stdio.h>`、`<stdlib.h>`、`<string.h>`；
2. 动态合成事件数组扩容：
   - 将 `malloc` 替换为 `HeapAlloc(GetProcessHeap(), 0, ...)`；
   - 将 `realloc` 替换为 `HeapReAlloc(GetProcessHeap(), 0, ...)`；
   - 将 `free` 替换为 `if (ptr) HeapFree(GetProcessHeap(), 0, ptr)`；
3. 日志函数 `ttp_diag_log` 改造：
   - 移除 `fopen` 与 `fprintf`；
   - 使用 Win32 `wvsprintfA` 格式化日志字符串；
   - 调用 `CreateFileA("ttp_debug.log", FILE_APPEND_DATA, FILE_SHARE_READ, NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL)` 配合 `WriteFile` 写入并 `CloseHandle`；
   - 同步调用 `OutputDebugStringA`；
4. 字符串与内存操作改用 `__builtin_memcpy` / `__builtin_memset` / `lstrlenA`。

- [ ] **Step 2: 编译并运行引擎测试套件**

```powershell
gcc -Os -Ireverse-gemini/src reverse-gemini/tests/test_engine.c reverse-gemini/src/ttp_storage.c reverse-gemini/src/ttp_vision.c reverse-gemini/src/ttp_engine.c -lole32 -loleaut32 -loleacc -lgdi32 -luser32 -o reverse-gemini/tests/test_engine.exe
.\reverse-gemini\tests\test_engine.exe
```
预期：PASS（动作合成序列、按键映射与回放调度全部通过）。

- [ ] **Step 3: 提交 Task 3**

```bash
git add reverse-gemini/src/ttp_engine.c
git commit -m "refactor(engine): migrate event buffer allocation and logging to Win32 native APIs"
```

---

### Task 4: `tinytask_pro.c` 自定义入口点与 Win32 原生格式化收敛

**Files:**
- Modify: `reverse-gemini/src/tinytask_pro.c:1-2950`
- Test: `reverse-gemini/tests/test_tinytask_pro.c`

**Interfaces:**
- Consumes: Win32 API (`wsprintfA`, `lstrcpynA`, `lstrcpyA`, `lstrlenA`, `GetProcessHeap`, `HeapAlloc`, `HeapFree`), `WinMainCRTStartup`
- Produces:
  - 纯原生 Win32 入口点 `void WinMainCRTStartup(void)`
  - 原生主窗口消息循环 `WndProc`
  - 原生微调弹窗过程 `StepEditDlgProc`

- [ ] **Step 1: 改造 `src/tinytask_pro.c` 移除 CRT 依赖**

1. 移除 `#include <stdio.h>`、`<stdlib.h>`、`<string.h>`；
2. 在文件末尾或合适位置显式植入 `WinMainCRTStartup(void)`：
   - 使用 `GetModuleHandleA(NULL)` 获取实例句柄；
   - 健壮解析 `GetCommandLineA()` 过滤首个可执行文件参数；
   - 调用 `WinMain` 并调用 `ExitProcess(ret)`；
3. 将全文 27 处 `snprintf` 统一替换为 `wsprintfA`（`wsprintfA` 原生支持 `%d`、`%u`、`%s`、`%c`、`%X` 等格式符）；
4. 将所有 `malloc` / `free` 替换为 `HeapAlloc` / `HeapFree`；
5. 将 `strncpy` / `strcpy` 替换为 `lstrcpynA` / `lstrcpyA`，将 `memset` / `memcpy` 替换为 `__builtin_memset` / `__builtin_memcpy`。

- [ ] **Step 2: 编译测试并回归点检**

```powershell
gcc -Os -Ireverse-gemini/src reverse-gemini/tests/test_tinytask_pro.c reverse-gemini/src/ttp_storage.c reverse-gemini/src/ttp_vision.c reverse-gemini/src/ttp_engine.c -lcomctl32 -lole32 -loleaut32 -loleacc -lgdi32 -luser32 -lcomdlg32 -lshell32 -o reverse-gemini/tests/test_tinytask_pro.exe
.\reverse-gemini\tests\test_tinytask_pro.exe
```
预期：PASS。

- [ ] **Step 3: 提交 Task 4**

```bash
git add reverse-gemini/src/tinytask_pro.c
git commit -m "refactor(ui): embed WinMainCRTStartup entry point and migrate string formatting to wsprintfA"
```

---

### Task 5: 全量零 CRT 独立构建、双重硬门禁验收与全量测试回归

**Files:**
- Modify: `README.md`
- Build: `reverse-gemini/bin/tinytask_pro.exe`
- Test: 全量测试套件 (`test_storage.exe`, `test_chromakey.exe`, `test_masked_vision.exe`, `test_vision.exe`, `test_engine.exe`, `test_tinytask_pro.exe`, `test_fix_repro.exe`)

**Interfaces:**
- Consumes: 全量改造后的源码模块与全新构建指令
- Produces:
  - 产物体积 $\le 50\text{ KB}$ 的最终可执行文件 `bin/tinytask_pro.exe`
  - 纯净导入表（0 个 `api-ms-win-crt-*` 依赖）
  - 全量 100% 绿灯回归测试报告与更新的说明文档

- [ ] **Step 1: 执行零 CRT 终极构建**

```powershell
windres reverse-gemini/src/tinytask_pro.rc -O coff -o reverse-gemini/src/tinytask_pro_res.o
gcc -Os -s -mwindows -nostdlib -mno-stack-arg-probe -ffunction-sections -fdata-sections -fno-unwind-tables -fno-asynchronous-unwind-tables -fno-ident "-Wl,--gc-sections" reverse-gemini/src/tinytask_pro.c reverse-gemini/src/ttp_storage.c reverse-gemini/src/ttp_vision.c reverse-gemini/src/ttp_engine.c reverse-gemini/src/tinytask_pro_res.o -lkernel32 -luser32 -lgdi32 -lcomctl32 -lcomdlg32 -lshell32 -lole32 -loleaut32 -loleacc -o reverse-gemini/bin/tinytask_pro.exe
rm reverse-gemini/src/tinytask_pro_res.o
```

- [ ] **Step 2: 验证双重硬门禁**

1. **体积硬门禁**：
```powershell
Get-Item reverse-gemini/bin/tinytask_pro.exe | Select-Object Name, Length
```
断言：`Length <= 51200`（严格 $\le 50\text{ KB}$）。

2. **零 CRT 依赖纯净度门禁**：
```powershell
objdump -p reverse-gemini/bin/tinytask_pro.exe | Select-String -Pattern "api-ms-win-crt"
```
断言：输出必须为空！

- [ ] **Step 3: 运行全量测试套件回归验证**

```powershell
.\reverse-gemini\tests\test_storage.exe
.\reverse-gemini\tests\test_chromakey.exe
.\reverse-gemini\tests\test_masked_vision.exe
.\reverse-gemini\tests\test_vision.exe
.\reverse-gemini\tests\test_engine.exe
.\reverse-gemini\tests\test_tinytask_pro.exe
.\reverse-gemini\tests\test_fix_repro.exe
```
断言：所有测试全部通过（100% 绿灯）。

- [ ] **Step 4: 更新 README.md 构建命令与体积技术指标**

更新中英文 README.md，更新构建命令行（引入 `-nostdlib -mno-stack-arg-probe`）并将二进制体积里程碑更新为 **40 ~ 50 KB** 纯原生 Win32 零 CRT 架构。

- [ ] **Step 5: 最终交付提交**

```bash
git add README.md reverse-gemini/README.md reverse-gemini/bin/tinytask_pro.exe
git commit -m "release: achieve zero-CRT pure Win32 extreme binary shrink under 50KB"
```
