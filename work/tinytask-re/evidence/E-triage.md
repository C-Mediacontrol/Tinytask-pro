# Evidence: E-triage (TinyTask 1.77 逆向初始分诊)

## 1. 样本元信息
- **文件路径**: `Library\tinytask.exe`
- **文件大小**: 36,352 字节 (35.50 KB)
- **MD5**: `8fd3551654f0f5281ddbd7e32cb73054`
- **SHA256**: `75e06ac5b7c1adb01ab994633466685e3dcef31d635eba1734fe16c7893ffe12`
- **架构**: x86 (32-bit PE GUI)
- **Subsystem**: IMAGE_SUBSYSTEM_WINDOWS_GUI (2)
- **ImageBase**: `0x00400000`
- **EntryPoint**: `0x00404680`
- **编译器/链接器**: MSVC / 原生 Win32 C (无 CRT 膨胀，体积极度紧凑)
- **加壳情况**: 无壳（各节区熵均在 4.8 - 6.4 之间，代码节为 6.3762，属于正常编译机器码）

## 2. PE 节区概览
| 节区名 | 虚拟地址 (VA) | 虚拟大小 | 原始数据大小 | 熵 (Entropy) | 属性/用途 |
|---|---|---|---|---|---|
| `.text` | `0x00401000` | `0x3784` | `0x3800` | 6.3762 | 可执行代码 |
| `.rdata` | `0x00405000` | `0x0bc8` | `0x0c00` | 5.0986 | 只读数据、导入表、字符串 |
| `.data` | `0x00406000` | `0x0ffc` | `0x0a00` | 4.8045 | 全局变量与可读写状态 |
| `.rsrc` | `0x00407000` | `0x3de0` | `0x3e00` | 6.4040 | 位图、图标、版本信息、Manifest |

## 3. 导入表聚类与能力锚点 (E-imports 硬门)
- **质量标注**: `quality=clean, direct IAT` (无动态混淆，函数直接暴露)
- **Win32 GUI / 窗口管理 (USER32.dll)**:
  - `RegisterClassExA`, `CreateWindowExA`, `ShowWindow`, `UpdateWindow`, `GetMessageA`, `TranslateMessage`, `DispatchMessageA`, `DefWindowProcA`, `PostQuitMessage` (原生 Win32 消息循环，窗口类名为 `TinyTaskClass`)
  - `CreatePopupMenu`, `AppendMenuA`, `TrackPopupMenu`, `DestroyMenu` (右键/选项菜单)
  - `BeginPaint`, `EndPaint`, `DrawTextA`, `InvalidateRect`, `GetClientRect`, `GetWindowRect` (自定义工具栏按钮绘制与布局)
- **键鼠自动化核心 (USER32.dll)**:
  - 回放注入: `mouse_event`, `keybd_event` (Win32 经典合成输入)
  - 录制与状态监听: `GetAsyncKeyState`, `GetKeyState`, `GetCursorPos` (通过定时器/轮询或状态差分捕获键鼠，未采用复杂的全局 Hook DLL，保证了轻量性)
  - 定时驱动: `SetTimer`, `KillTimer` (驱动录制采样、状态计时、回放步进)
- **文件与持久化 (KERNEL32.dll & comdlg32.dll)**:
  - `CreateFileA`, `ReadFile`, `WriteFile`, `CloseHandle`, `GetFileSize`, `SetFilePointer` (`.rec` 录制文件与可执行文件打包)
  - `GetPrivateProfileStringA`, `WritePrivateProfileStringA`, `GetPrivateProfileIntA` (`.ini` 配置文件读写)
  - `GetOpenFileNameA`, `GetSaveFileNameA` (文件打开与保存对话框)
- **内存管理 (KERNEL32.dll)**:
  - `HeapAlloc`, `HeapReAlloc`, `HeapFree`, `GetProcessHeap` (使用系统默认堆，零 C 运行时堆依赖)
- **外部交互与提示**:
  - `ShellExecuteA` (访问官网 `https://www.tinytask.net`)
  - `MessageBoxA`, `MessageBoxIndirectA` (弹窗提示与错误警告)

## 4. 关键字符串与功能特征
- 窗口类名: `TinyTaskClass`
- 配置文件项: `play_key`, `record_key`, `hide_captions`, `topmost`, `speed_custom`, `speed`, `window_y`, `window_x`, `toolbar_padding`, `toolbar_image`
- 快捷键支持: `Control + Shift + Alt + P`, `Print Screen`, `Control + Shift + Alt + R`, `Pause/ScrollLock`
- 播放控制: `1x`, `2x`, `100x`, `Custom Speed`, `Continuous Playback`, `Set Playback Loops`
- 编译为 EXE 标记: `@@@@@`, `$$$$$`, `%05d`, `36352` (通过模板克隆/追加录制载荷生成独立小 EXE)

## 5. 初步假设清单 (Hypotheses)
1. **[H1: 录制机制]** TinyTask 未使用 `SetWindowsHookEx`，而是通过高频 `SetTimer` 结合 `GetAsyncKeyState` 与 `GetCursorPos` 进行差分采样，或者在窗口处于前台时监听消息。
2. **[H2: 数据结构]** 录制序列为一个由时间戳（或时间增量 dt）、事件类型（鼠标移动/按键按下/释放/按键代码/坐标）组成的紧凑结构体数组。
3. **[H3: 自生成 EXE 机制]** "Compile to EXE" 实际上是读取自身二进制（`36352` 字节），将录制数据和参数写在 EXE 末尾，并在特定标记处打补丁。
4. **[H4: 目标产物体积 <50KB]** 使用纯 C 语言编写原生 Win32 API 代码，由已发现的 MinGW-w64 `gcc -Os -s -mwindows` 编译，不链接重型库，产物可控制在 15KB~30KB 左右。
