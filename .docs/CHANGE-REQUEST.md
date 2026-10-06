## 类型：🐛 Bug 修复

### 摘要
修复右键桌面图标弹出菜单时程序卡死 0.5s 并因栈保护页越界崩溃 (0xC0000005) 的问题。

### 根因 / 背景
1. **栈越界踩空崩溃 (0xC0000005)**: `ttp_vision.c` 中的 `ttp_adaptive_crop_button` 在函数栈上分配了 4 个 64KB 二维数组（`gray`, `edge`, `dilated`, `closed`，合计 256KB）。在最近引入的 Zero-CRT 极简化编译参数 `-mno-stack-arg-probe` 约束下，GCC 禁用了栈探测探针 `___chkstk_ms`。函数入口 `sub $0x40108, %rsp` / `sub $0x400ac, %esp` 一次性越过 Windows 4KB 栈保护页（Guard Page），在未提交页面写入时被内核以 `0xC0000005` 强制杀死。
2. **跨进程模态菜单卡死 ~0.5s**: 桌面右键上下文菜单（`#32768` 窗口）弹出时，Windows Explorer 进入模态菜单跟踪循环。`RecTimerProc` 在此时调用 `AccessibleObjectFromPoint` 会在跨进程 COM RPC 查询中被 Explorer 模态泵阻塞约 0.5 秒。超时返回 `hasAcc = FALSE` 后无图片缓存，强制进入上述 `ttp_adaptive_crop_button` 导致程序致命崩溃。

### 方案
- **原始逻辑**:
  - `ttp_adaptive_crop_button` 在栈上声明 256KB 局部数组。
  - `RecTimerProc` 对所有坐标无差别调用 `AccessibleObjectFromPoint`，即使在系统模态菜单 `#32768` 上也是如此；若无图片缓存则一律调用 `ttp_adaptive_crop_button`。
- **新逻辑**:
  - `ttp_adaptive_crop_button` 内部改为单次 `HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, 256 * 256 * 4)` 从堆中申请工作缓冲块，指针细分映射给 `gray`, `edge`, `dilated`, `closed`，并在所有退出分支（以及异常保护路径）统一 `HeapFree`，彻底消除栈压力（栈帧控制在 1KB 以内）。
  - `RecTimerProc` 增加系统模态菜单短路防护：通过 `WindowFromPoint` 配合窗口类名探测（`#32768`），遇到菜单窗口或右键操作时，跳过耗时且易阻塞的跨进程 COM MSAA 查询，直接记录标准坐标/按键事件，消除 0.5s 卡死。
- **修改方式**:
  - 修改 `reverse-gemini/src/pro/ttp_vision.c` 中的 `ttp_adaptive_crop_button`，实现堆化分配与释放。
  - 修改 `reverse-gemini/src/pro/tinytask_pro.c` 中的 `RecTimerProc`，增加模态菜单短路与事件容错。
  - 在 `reverse-gemini/tests/` 中编写专属回归测试用例，并在 x86 和 x64 两个变体上全绿回归验证。

### 影响面
- 上游（生产方）: `RecTimerProc` 录制逻辑，鼠标点击事件流
- 下游（消费方）: `ttp_adaptive_crop_button` 自适应边界裁切，`tinytask_pro.exe` (x64), `tinytask_pro_x86.exe` (x86)
- 风险等级: 🟢 低（将局部大数组移入堆并为模态菜单添加前置短路保护，不破坏既有录制契约与回放逻辑）

---
**审批**: 已批准
**状态**: 已实施已验证 (GREEN)
