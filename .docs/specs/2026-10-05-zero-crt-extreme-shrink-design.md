# TinyTask Pro: 零 CRT 纯原生 Win32 极限瘦身设计规范

**文档编号:** SPEC-20261005-03  
**状态:** 已批准 (Approved)  
**日期:** 2026-10-05  
**目标目录:** `reverse-gemini/`  

---

## 1. 背景与核心目标 (Context & Objectives)

经过对当前编译输出的可执行文件 `bin/tinytask_pro.exe` 进行 PE 节区和 Link Map 底层逆向解析，当前二进制体积为 **100,864 字节 (~98.5 KB)**。

经深度诊断分析，其中超过 **50% 的体积属于非业务核心开销**：
1. **MinGW-w64 运行时启动桩与胶水代码**：`crt2.o`、`pseudo-reloc.o`、`pesect.o`、`tlssup.o` 等占用约 **12.4 KB**；
2. **libgcc 辅助桩**：`_chkstk_ms` 占用约 **5.2 KB**；
3. **UCRT 胶水与导入表**：`api-ms-win-crt-*` 动态库导入表与兼容桩占用约 **13.0 KB**；
4. **节区对齐碎片**：12 个 Section 各自按 512 字节对齐产生的填充间隙约 **1.5 KB**。

原版 TinyTask 1.77 体积仅为 **35 KB**，其之所以极致精悍，核心原因在于**零 CRT（Zero-CRT）依赖**——完全基于原生 Win32 API 编写并使用自定义入口点 `WinMainCRTStartup`。

### 核心目标与量化指标
* **体积终极门禁**：编译产物 `bin/tinytask_pro.exe` 物理体积严格压降至 **40 ~ 50 KB** 区间（腰斩 50%~60% 体积）；
* **功能零删减**：100% 完整保留 TinyTask Pro 独有的抽屉式 UI、步骤列表、掩码搜图引擎、60FPS 棋盘格微调弹窗及超时策略机制；
* **零 CRT 依赖纯净度**：通过手工重构将所有模块全面切换为纯 Win32 原生 API，剥离全部 9 个 `api-ms-win-crt-*` 导入项，实现真正的纯净原生 Win32 独立可执行文件。

---

## 2. 总体技术架构 (Architecture Overview)

```
+-----------------------------------------------------------------------------------+
|                        TinyTask Pro: 零 CRT 纯原生架构                            |
+-----------------------------------------------------------------------------------+
|                                                                                   |
|  [PE 启动入口] WinMainCRTStartup(void)                                            |
|       │                                                                           |
|       ├─ GetModuleHandleA(NULL) 提取实例句柄                                      |
|       ├─ GetCommandLineA() 手工跳过路径参数                                       |
|       └─ 调用 WinMain() -> ExitProcess(ret) 退出                                  |
|                                                                                   |
|  [纯原生内存管理]                                                                 |
|       ├─ 分配: HeapAlloc(GetProcessHeap(), 0, size)                               |
|       ├─ 零清: HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, size)                |
|       ├─ 扩容: HeapReAlloc(GetProcessHeap(), 0, ptr, size)                        |
|       └─ 释放: if (ptr) HeapFree(GetProcessHeap(), 0, ptr)                        |
|                                                                                   |
|  [纯原生文件与系统 I/O]                                                           |
|       ├─ 存储: CreateFileA / ReadFile / WriteFile / SetFilePointer / CloseHandle  |
|       ├─ 格式化: wsprintfA (替换全部 snprintf)                                    |
|       └─ 内存块: __builtin_memcpy / __builtin_memset / __builtin_memcmp           |
|                                                                                   |
|  [构建链极致瘦身编译指令]                                                         |
|       └─ gcc -Os -s -mwindows -nostdlib -mno-stack-arg-probe                      |
|              -ffunction-sections -fdata-sections                                  |
|              -fno-unwind-tables -fno-asynchronous-unwind-tables -fno-ident        |
|              -Wl,--gc-sections                                                    |
|              -lkernel32 -luser32 -lgdi32 -lcomctl32 -lcomdlg32 -lshell32          |
|              -lole32 -loleaut32 -loleacc                                          |
|                                                                                   |
|  [最终产物] bin/tinytask_pro.exe: 40 ~ 50 KB, 0 CRT DLLs                          |
+-----------------------------------------------------------------------------------+
```

---

## 3. 详细设计规范 (Detailed Design)

### 3.1 显式入口点与生命周期 (`src/tinytask_pro.c`)
在 `tinytask_pro.c` 中显式定义 Win32 PE 入口点：
```c
void WinMainCRTStartup(void) {
    HINSTANCE hInst = GetModuleHandleA(NULL);
    LPSTR lpCmdLine = GetCommandLineA();

    /* 健壮解析命令行参数，跳过首个可执行文件名/路径 token */
    if (*lpCmdLine == '"') {
        lpCmdLine++;
        while (*lpCmdLine && *lpCmdLine != '"') lpCmdLine++;
        if (*lpCmdLine == '"') lpCmdLine++;
    } else {
        while (*lpCmdLine && *lpCmdLine > ' ') lpCmdLine++;
    }
    while (*lpCmdLine && *lpCmdLine <= ' ') lpCmdLine++;

    int ret = WinMain(hInst, NULL, lpCmdLine, SW_SHOWDEFAULT);
    ExitProcess((UINT)ret);
}
```

### 3.2 模块级 Win32 原生 API 重构规范

#### 3.2.1 持久化存储模块 (`src/ttp_storage.c` & `src/ttp_storage.h`)
* 彻底剥离 `<stdio.h>`, `<stdlib.h>`, `<string.h>`；
* 文件读取流程：
  * `CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL)`；
  * `ReadFile(hFile, buf, size, &dwRead, NULL)`；
  * `SetFilePointer(hFile, (LONG)offset, NULL, FILE_BEGIN)` 替代 `fseek`；
  * `CloseHandle(hFile)` 替代 `fclose`。
* 文件写入流程：
  * `CreateFileA(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL)`；
  * `WriteFile(hFile, buf, size, &dwWritten, NULL)`；
  * `CloseHandle(hFile)`。
* 内存管理：全面使用 `HeapAlloc(GetProcessHeap(), ...)` 与 `HeapFree`。

#### 3.2.2 视觉引擎模块 (`src/ttp_vision.c` & `src/ttp_vision.h`)
* 彻底剥离 `<stdlib.h>`, `<string.h>`；
* 图像与掩码内存管理：
  * `ttp_crop_rect_bmp`、`ttp_chromakey_mask` 等内部动态分配使用 `HeapAlloc` / `HeapFree`；
* 数学与内存块处理：
  * `abs()` 统一替换为编译器内联 `__builtin_abs()`；
  * 内存清空与复制统一采用 `__builtin_memset` 与 `__builtin_memcpy`；
  * 字符串长度与拷贝采用 `lstrlenA` 与 `lstrcpynA`。

#### 3.2.3 调度引擎模块 (`src/ttp_engine.c` & `src/ttp_engine.h`)
* 彻底剥离 `<stdio.h>`, `<stdlib.h>`, `<string.h>`；
* 动态事件缓冲扩容：
  * `HeapAlloc` / `HeapReAlloc` / `HeapFree`；
* 诊断日志输出 (`ttp_diag_log`)：
  * `CreateFileA("ttp_debug.log", FILE_APPEND_DATA, FILE_SHARE_READ, NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL)`；
  * `wsprintfA` 格式化后直接 `WriteFile` 写入；
  * 同步调用 `OutputDebugStringA` 提供调试输出。

#### 3.2.4 主窗口与步骤微调弹窗 (`src/tinytask_pro.c`)
* 彻底替换 27 处 `snprintf` 为 Win32 `wsprintfA`；
* 字符串拷贝替换为 `lstrcpynA` 与 `lstrcpyA`；
* 维持原生 TrackBar、60FPS 灰白棋盘格与 Alpha 混合预览绘制不变。

---

## 4. 构建与链接流水线规范 (Build & Link Pipeline)

```powershell
# 1. 编译 Win32 原生资源
windres reverse-gemini/src/tinytask_pro.rc -O coff -o reverse-gemini/src/tinytask_pro_res.o

# 2. 纯原生 Win32 零 CRT 极致优化编译
gcc -Os -s -mwindows -nostdlib -mno-stack-arg-probe \
    -ffunction-sections -fdata-sections \
    -fno-unwind-tables -fno-asynchronous-unwind-tables -fno-ident \
    "-Wl,--gc-sections" \
    reverse-gemini/src/tinytask_pro.c \
    reverse-gemini/src/ttp_storage.c \
    reverse-gemini/src/ttp_vision.c \
    reverse-gemini/src/ttp_engine.c \
    reverse-gemini/src/tinytask_pro_res.o \
    -lkernel32 -luser32 -lgdi32 -lcomctl32 -lcomdlg32 -lshell32 -lole32 -loleaut32 -loleacc \
    -o reverse-gemini/bin/tinytask_pro.exe

# 3. 清理临时编译资源对象
rm reverse-gemini/src/tinytask_pro_res.o
```

---

## 5. 验收标准与测试矩阵 (Acceptance & Test Matrix)

1. **测试套件全量回归**：
   * `tests/test_storage.exe`：验证原生 `CreateFileA`/`ReadFile` 读写工程以及 V1 24bpp 资产升维行为；
   * `tests/test_chromakey.exe`：验证原生堆下的 BFS 连通泛洪抠图；
   * `tests/test_masked_vision.exe`：验证掩码归一化互相关定点搜图算法（2560×1440 极端壁纸得分 $\ge 0.90$）；
   * `tests/test_engine.exe`：验证回放调度与超时策略；
   * `tests/test_tinytask_pro.exe`：验证窗口注册与微调弹窗生命周期；
   * `tests/test_fix_repro.exe`：全量历史缺陷回归 100% 绿灯。
2. **终极体积硬门禁**：
   * `bin/tinytask_pro.exe` 物理文件大小必须满足：`Length <= 51200`（严格 $\le \mathbf{50\text{ KB}}$）。
3. **零 CRT 依赖纯净度门禁**：
   * 运行 `objdump -p bin/tinytask_pro.exe`，导入表不得存在任何 `api-ms-win-crt-*` 或 `msvcrt.dll` 依赖项。
