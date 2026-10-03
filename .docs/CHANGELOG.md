# Changelog

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
