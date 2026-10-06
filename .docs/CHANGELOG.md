# Changelog

## [1.7.0] - 2026-10-06

### Added & Fixed
- **按钮光标悬停与常态视觉差异解决 (Plan A + Plan D 组合拳)**:
  - **方案 A 原位预悬停嗅探 (Speculative Pre-Hover Probe)**: 在 `ttp_playback_step` 中统一视觉检索流程。当 ROI 候选区域最佳相似度处于边缘置信区（$0.45 \le \text{score} < \text{baseMinScore}$）时，若为真实回放桌面（`!s_hdcScreenOverride`），将光标临时预移动至候选点等待 35ms 激活宿主控件的 `WM_MOUSEMOVE` 悬停高亮渲染；重新取景复测后若得分跃升至 $\ge 0.65$，立即锁定目标并点击。
  - **方案 D 原位空间自适应门限 (Spatial Adaptive Gate)**: 针对距离录制坐标误差 $\le 20\text{px}$ 的紧邻原位候选区域，将置信门限适度放宽至 $0.55$，有效避免因微弱底色/未悬停常态差异导致的误判或超时。
  - **粗筛 Probe SAD 门限自适应**: 在 `match_gray_buffer_masked_ncc` 中，当 `minScore <= 0.60` 时自动将探针 SAD 容差由 60 放宽至 95，彻底避免轻微明度偏移的目标被粗筛算法误杀。
- **x86 与 x64 双架构原生最新二进制发布**:
  - 定位本地 `w64devkit-i686` 编译器并重新构建 `tinytask_pro_x86.exe`（69.5 KB，71,168 字节，0 CRT 依赖），彻底清除旧版二进制遗留的 256KB 栈越界与 0.5s 菜单卡死；
  - 重新构建 `tinytask_pro.exe`（76.0 KB，77,824 字节，0 CRT 依赖），双架构全套 8 大测试套件 49+ 测试项全绿通过。

## [1.6.0] - 2026-10-06

### Fixed
- **桌面图标右键模态菜单卡死与栈越界崩溃修复 (0xC0000005)**:
  - **堆化自适应边界工作缓冲 (消除栈保护页踩空)**: `ttp_vision.c` 中的 `ttp_adaptive_crop_button` 将原先在局部栈上声明的 4 个 64KB 大数组（`gray`, `edge`, `dilated`, `closed`，合计 256KB）全面改造为单次 `HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, 256 * 256 * 4)` 堆分配，并在全部退出分支集中释放。彻底根治了 `-mno-stack-arg-probe` 缺少栈探测探针时越过 4KB Guard Page 触发的 `0xC0000005` Access Violation 致命异常。
  - **系统模态菜单短路防护 (消除 ~0.5s 卡死)**: 在 `RecTimerProc` 中增加系统模态菜单窗口探测（`#32768`）。当检测到右键点击或指针位于上下文菜单上时，旁路耗时且阻塞的跨进程 COM `AccessibleObjectFromPoint` RPC 查询与视觉裁切，直接录制纯物理坐标与按键动作，消除 Windows Explorer 模态循环引起的 ~0.5s 界面假死。
  - **回归与健壮性验证**: 增加包含 256KB 堆隔离与 `#32768` 模态短路保护在内的第 15 项专项回归测试，全套 8 大测试套件（48+ 项）全绿通过，保持 0 CRT 纯原生 Win32 依赖与 $\le 78\text{ KB}$ (x64: 75.5 KB) 极致体积门禁。

## [1.5.0] - 2026-10-06

### Fixed
- **宏回放原位文本被视觉错误否决修复（回归原版无障碍文本优先逻辑）**:
  - **回归原版 Fast Check 逻辑**: 在 `ttp_playback_step` 中，当原位 `origPt` 处的 MSAA 返回与 `step->textKey` 匹配的文本时，直接判定原位命中并执行点击，彻底移除原位强加的 `TEXT_FAST_CHECK_VALIDATE` 视觉一票否决门槛。视觉模板仅保留用于原位未命中时的全屏候选点交叉验证与纯视觉找图。
  - **局部搜图与边界色差抗扰**: 在 `match_gray_buffer_masked_ncc` 中，对极小邻域微调旁路 Probe SAD 粗筛，针对全屏搜图内缩探测点，彻底杜绝壁纸/悬停色差导致的误杀。
  - **桌面图标裁剪掩码升级**: `ttp_crop_rect_bmp` 生成标准 32bpp BGRA 模板并自动施加色度键（chromakey）去背掩码，对存量 24bpp 宏保持 100% 向后兼容。
  - **Zero-CRT 调试日志浮点数修复**: 纯 Win32 下以 `%d.%04d` 定点整数点算法格式化相关度得分，彻底根除 `score=f` 字符丢失以及 x64 调用约定下未消费 8 字节 `double` 引起的栈寄存器错位和坐标数值乱码（如 `pos=(1082135273, 37)`）。
  - **零 CRT 与体积门禁**: 保持纯原生 Win32，0 CRT 依赖，单文件体积 83.0 KB（严格满足 $\le 85\text{ KB}$ 硬门槛）。

## [1.4.0] - 2026-10-04

### Fixed
- **目标移开后回放误点左上角修复（对齐 Power Automate 容差架构与原版 TinyTask 物理像素硬锁定）**:
  - **金字塔粗检置信门槛（Tolerance Gate）**: 在 `ttp_match_template_ncc` 中加入 `bestCoarseScore >= 0.35` 严格置信度下限。当目标按钮移开或屏幕特征不足时，粗搜未命中立即放弃并进入超时兜底流程，彻底消除将初始值 $(0, 0)$ 喂入局部精修锁死在屏幕左上角 $[0..8, 0..8]$ 的伪峰值缺陷。
  - **对齐原版 TinyTask 输入时序与物理像素锁定**: 将 `ttp_playback_step` 中 `mouse_event` 与 `SetCursorPos` 的调用时序调整为与官方原版完全一致（先派发 `mouse_event`，随即调用 `SetCursorPos` 硬校准），彻底消除由于归一化绝对坐标（0~65535）浮点截断或多屏/DPI 漂移反向覆盖物理像素的问题。
  - **Power Automate 智能双轨锚定（父窗口标题过滤）**: 在 `RecTimerProc` 中，若 Accessible Name 获取到的仅是宿主顶级窗口标题（如“计算器”、“未命名 - 记事本”），自动剥离顶层标题，将主模式确定为 `TTP_TARGET_IMAGE`（以图像为第一主锚点），确保移动后的按钮完全依靠视觉图像精准锁定，避免被顶级窗口容器误导。
  - **截屏与方差健康防御**: 在 `ttp_adaptive_crop_button` 中显式校验 `BitBlt` 返回值，并计算截取区域的灰度方差。对纯白/纯黑无纹理的异常截取（方差 $\le 1.0$）安全拦截并返回 `FALSE`，杜绝生成零方差死模板。
  - **Per-Monitor DPI Aware 支持**: 在 `tinytask_pro.manifest` 中注入 `PerMonitorV2` DPI 感知声明，并在 `WinMain` 首行动态初始化 DPI 感知，确保所有 GDI 设备上下文与屏幕坐标在任何缩放比例下均实现 1:1 绝对物理像素对齐。

## [1.3.0] - 2026-10-04

### Fixed
- **多键组合快捷键触发异常修复（对齐原版 TinyTask 1.77 并引入上升沿锁存器）**:
  - **双重保障上升沿锁存器（Rising-Edge Latch）**: 在 `HotkeyTimerProc` 中引入静态锁存变量 `s_recTriggerWasDown` 与 `s_playTriggerWasDown`，严格保证只在按键按下瞬间（0->1 上升沿）触发单次命令，双手长按组合键期间保持锁存，物理按键完全释放后才解除锁定。彻底根除了因双手组合键长按（250~450ms）导致的电平重复触发及录制瞬间开闭翻转缺陷。
  - **移除 UI 线程 Sleep(150)**: 彻底移除了录制分支下的 `Sleep(150)` 阻塞调用，消除 UI 消息循环卡死与定时器堆叠，实现 0ms 零延迟极速触发。
  - **全键盘物理状态预播种（State Seeding）**: 对齐原版 TinyTask 1.77（反汇编 `0x402340 ~ 0x402361`），在 `StartRecording` 启动时遍历 VK 1~254 预读取并播种 `g_LastKeyState`，避免用户松开启动热键时被录制为伪按键动作。
  - **尾部热键残影回溯修剪（Trailing Hotkey Pruning）**: 对齐原版 TinyTask 1.77（反汇编 `0x4023d8 ~ 0x402419`），在 `StopRecording` 中增加 `IsTrailingHotkeyStep` 回溯修剪逻辑，自动剔除尾部用于停止录制的热键动作。
  - **采样率对齐原版**: 将 `TIMER_HOTKEY` 周期由 50ms 调整为 25ms（40Hz），完全消除快速敲击（30~40ms）落在 50ms 盲区的问题。
  - **自定义组合键容差匹配**: 自定义热键改为掩码包含容差匹配（`((mod & hk->customMod) == hk->customMod)`），包容多指触键毫秒级微小时差。

## [1.2.0] - 2026-10-04

### Fixed
- **UI 字符乱码消除**:
  - 抽屉操作按钮（`+ Add`, `- Del`, `Up`, `Down`, `Run Step`）与 Prefs 菜单项全部切换为标准纯 ASCII 字符，彻底解决 Windows 简体中文环境（CP936）下多字节解码为乱码（`[脑 Del]`, `[鋁?Up]` 等）的问题。
- **Prefs 菜单即时渲染修复 (白屏问题彻底解决)**:
  - 工具栏按钮点击由同步 `SendMessage` 改为异步 `PostMessage`，避免在 `WM_LBUTTONDOWN` 下阻塞进入 Windows 拖拽模态导致菜单窗口延迟绘制；
  - 规范调用 `SetForegroundWindow` 与 `PostMessage(WM_NULL)` 维持菜单焦点生命周期；
  - 在 `WM_INITMENUPOPUP` 中强制刷新 `#32768` 菜单窗口；
  - 主窗口追加 `WS_CLIPCHILDREN` 样式，杜绝父窗口背景绘制抹除子控件；
  - 动态读取注册表 `HKCU\Software\Microsoft\Windows\CurrentVersion\Themes\Personalize\AppsUseLightTheme`，浅色系统保持系统原生 GDI 渲染。
- **Power Automate 全屏视觉对齐与 Dual-Engine Fallback (解决移动目标判定过窄)**:
  - **Dual-Engine Visual Fallback**: 在 `ttp_playback_step` 中，当 `TTP_TARGET_TEXT` 无法通过可访问性树定位文本时，立即自动调用步骤内保存的 BMP 模板执行全屏 NCC 图像匹配，实现任意窗口拖拽移动的无感自动定位。
  - **抗混叠金字塔全屏检索**: 采用 2x 盒式平滑滤波生成粗筛金字塔，彻底根治跨步长采样引起的高频边缘 Nyquist 混叠（反相抵消），并在候选峰周围执行全分辨率精细拟合与方差底限过滤。在 2560×1440 屏幕下全屏检索耗时稳定在 30~47ms，匹配相关度达 1.000。

## [1.1.0] - 2026-10-04

### Fixed
- **录制自点击过滤与微抖动支持**: `RecTimerProc` 过滤 TinyTask Pro 主窗口及其子控件，避免点击 Stop 按钮被录制。修复 `g_LastKeyState` 的 `BYTE` 类型按位检查逻辑，确保鼠标 UP 事件和键盘状态转换精确捕获。提高拖拽阈值（动态兼容 DPI 与人类微抖动），解决正常点击被误判为 `TTP_ACTION_DRAG` 的缺陷。
- **图像匹配性能优化 (Power Automate 架构)**:
  - Tier-1: 优先在原始点击坐标周围 $\pm 200\text{px}$ ROI 范围内局部搜索，单次耗时 $<3\text{ms}$。
  - Tier-2: 局部未命中时，采用 4x 降采样金字塔粗搜 + 原图精验，将 2560×1440 屏幕单次匹配耗时从 17,203ms 降至 16ms，彻底根治回放 17.2s 卡顿。
- **超时处理对话框注册与步骤级策略**:
  - 在 `ttp_engine.c` 中显式注册 `TTPTimeoutDialog` 窗口类，消除窗口创建失败隐患。
  - 支持每步配置超时失败行为（0=弹窗提问, 1=重试, 2=使用录制坐标, 3=跳过, 4=停止），并在 ListView Drawer 中直接展示与双击原地编辑。
- **UI 无障碍支持**:
  - 集成 `AccessibleObjectFromPoint`，录制时自动捕获目标控件的无障碍 Accessible Name（支持 Chromium/Edge 等现代化渲染树）。
