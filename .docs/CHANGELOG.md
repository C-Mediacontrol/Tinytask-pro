# Changelog

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
