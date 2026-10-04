# Evidence: E-static (TinyTask 1.77 静态逆向与逻辑还原)

## 1. 核心架构与模块划分
通过 Capstone 反汇编与交叉引用分析，TinyTask 1.77 完全由原生 Win32 C 编写，核心包含以下几个关键子系统：

```
+-------------------------------------------------------------+
|                     TinyTask 1.77 (x86)                     |
+-------------------------------------------------------------+
| 1. WinMain (0x401000): 命令行解析、INI 加载、窗口类注册       |
| 2. WndProc (0x401489): 消息循环分发、自定义工具栏绘制        |
| 3. Recording Engine (0x403300): 10ms 轮询差分采样器         |
| 4. Playback Engine (0x4034C6): 定时器驱动、坐标归一化注入    |
| 5. File I/O (0x402F8E / 0x401EA0): .rec 读写与 EXE 打包     |
| 6. Hotkey Monitor (0x4018EF): 1005 定时器轮询快捷键         |
+-------------------------------------------------------------+
```

## 2. 录制数据结构定义 (`TinyTaskEvent`)
TinyTask 在内存及 `.rec` 文件中采用严格的 20 字节（`0x14` 字节）紧凑结构体序列：

```c
#pragma pack(push, 1)
typedef struct {
    DWORD uMsg;      /* 0x00: 事件类型 (Win32 消息 ID)
                      *   0x0200 = WM_MOUSEMOVE
                      *   0x0201 = WM_LBUTTONDOWN
                      *   0x0202 = WM_LBUTTONUP
                      *   0x0204 = WM_RBUTTONDOWN
                      *   0x0205 = WM_RBUTTONUP
                      *   0x0100 = WM_KEYDOWN
                      *   0x0101 = WM_KEYUP
                      */
    DWORD param1;    /* 0x04: 
                      *   鼠标事件: X 屏幕物理坐标
                      *   键盘事件: (MapVirtualKey(vk) << 8) | vkCode
                      */
    DWORD param2;    /* 0x08:
                      *   鼠标事件: Y 屏幕物理坐标
                      *   键盘事件: 扫描码 / 辅助标志
                      */
    DWORD timestamp; /* 0x0C: GetTickCount() 毫秒时间戳 */
    DWORD hwnd;      /* 0x10: 触发事件时的前台窗口句柄 (HWND) */
} TinyTaskEvent;     /* 大小: 20 字节 (0x14) */
#pragma pack(pop)
```

## 3. 录制机制 (Recording Subsystem)
- **定时器与采样频率**: 通过 `SetTimer(hWnd, 1001, 10, NULL)` 开启 10 毫秒高精度轮询（100 Hz 采样率）。
- **无 DLL 全局 Hook**: 未使用 `SetWindowsHookEx`，而是通过 `GetAsyncKeyState` (0~255) 与 `GetCursorPos` 进行状态快照对比。
- **状态差分检测**:
  - 维护一个大小为 256 字节的上一周期按键状态表 `0x406D20`。
  - 每 10ms 遍历 256 个虚拟键，若发现按键状态翻转（按下/释放），则生成 `WM_KEYDOWN` / `WM_KEYUP` 事件写入动态缓冲区 `0x4069F0`。
  - 获取鼠标坐标 `GetCursorPos`，若坐标与上一采样点不同，则记录 `WM_MOUSEMOVE` 事件。
  - 检测 `VK_LBUTTON` 与 `VK_RBUTTON`，生成鼠标点击与释放事件。
  - 缓冲区动态扩容步长为 `0x4E20` 字节（20,000 字节，即 1,000 个事件）。

## 4. 回放机制 (Playback Subsystem)
- **启动预处理 (0x402148)**:
  - 释放所有当前处于按下状态的按键与鼠标按键（发送合成 `keybd_event` 与 `mouse_event` UP 事件）。
  - 清理键盘状态 `SetKeyboardState`，冲刷输入队列。
- **时序与步进 (0x4036C0 - 0x403807)**:
  - 计算相邻事件的时间差 `dt = event[i].timestamp - event[i-1].timestamp`。
  - 根据回放速度调节延时：
    - 正常速度 (1x): `delay = dt`
    - 加速模式 (2x, 100x, custom): `delay = dt / speed`
    - 100x 极速模式: 对密集鼠标移动进行 6 步抽样，结合 `Sleep(1)` 避免 Windows 消息队列阻塞。
  - 通过 `SetTimer(hWnd, 1002, delay, NULL)` 驱动下一事件。
- **合成输入注入**:
  - 鼠标移动：将屏幕物理坐标归一化到 `0..65535` 空间：
    `dx = (x * 65535) / GetSystemMetrics(SM_CXSCREEN)`
    `dy = (y * 65535) / GetSystemMetrics(SM_CYSCREEN)`
    调用 `mouse_event(MOUSEEVENTF_ABSOLUTE | MOUSEEVENTF_MOVE, dx, dy, 0, 0)` 并辅以 `SetCursorPos(x, y)`。
  - 鼠标点击：调用 `mouse_event(MOUSEEVENTF_LEFTDOWN / LEFTUP / RIGHTDOWN / RIGHTUP)`。
  - 键盘输入：调用 `keybd_event(vkCode, scanCode, flags, 0)`。
- **紧急停止**: 每周期轮询 `GetAsyncKeyState(VK_PAUSE)` 与 `GetAsyncKeyState(VK_SCROLL)`，一经触发立即中断回放。

## 5. 文件存储与独立 EXE 编译机制
- **`.rec` 录制文件格式**:
  - 文件内容即为 `TinyTaskEvent` 数组的纯二进制原始流（Raw Binary Stream）。
  - 校验规则：文件大小必须大于 2 字节且是 20 的整数倍 (`filesize % 20 == 0`)。若不符合则弹出警告："This file does not appear to be a valid recording. Load anyway?"。
- **"Compile to EXE" 生成独立可执行文件机制 (0x401EA0 - 0x402050)**:
  - 读取自身 EXE 模板（`36,352` 字节）。
  - 搜索模板中的占位标记字符串：
    - `@@@@@`: 替换为 `%05d` 格式的速度倍率。
    - `$$$$$`: 替换为 `%05d` 格式的循环次数。
  - 将 PE OptionalHeader 中的 Checksum 字段清零 (`PEHeader + 0x58`)。
  - 将录制的 `.rec` 原始二进制数据直接追加在 EXE 文件末尾（Overlay 附加数据）。
  - 编译后的 EXE 启动时检测自身文件大小，若大于自身模板大小，则直接从末尾解出录制数据并自启动回放。

## 6. 工具栏与菜单命令映射表
- **工具栏 6 大按钮**:
  - `0x8000`: 打开录制文件 (`.rec`)
  - `0x8001`: 保存录制文件 (`.rec`)
  - `0x8002`: 录制开始 / 停止
  - `0x8003`: 回放开始 / 停止
  - `0x8004`: 编译为独立 EXE
  - `0x8005`: 选项菜单 (Options)
- **回放速度**:
  - `0x8006`: 原速
  - `0x8007`: 1x
  - `0x8008`: 2x
  - `0x8009`: 自定义速度
  - `0x800a`: 100x 极速
  - `0x800b`: 连续回放 (Continuous Playback)
  - `0x800c`: 设置循环次数 (Set Playback Loops)
- **快捷键方案**:
  - 录制快捷键: `0x800f` (Ctrl+Shift+Alt+R), `0x8010` (PrintScreen), `0x8011` (F8), `0x8012` (F12)
  - 回放快捷键: `0x8013` (Ctrl+Shift+Alt+P), `0x8014` (PrintScreen), `0x8015` (F8), `0x8016` (F12)
- **其它选项**:
  - `0x8017`: 窗口置顶 (Always on Top)
  - `0x8018`: 显示标题栏 (Show Captions)
  - `0x801a`: 关于 (About TinyTask)
  - `0x801b`: 访问官方网站
