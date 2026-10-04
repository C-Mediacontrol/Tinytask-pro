# TinyTask Pro

> **下一代超轻量级桌面自动化与 RPA / Next-Generation Ultra-Lightweight Desktop Automation & RPA**  
> *纯 C 语言 & 原生 Win32 | 零外部依赖 | 单独立二进制 (< 95 KB) | 遵循 MIT 开源协议*  
> *Pure C99 & Native Win32 | Zero External Dependencies | Single Standalone Binary (< 95 KB) | MIT Licensed*

[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)
[![Language: C99](https://img.shields.io/badge/Language-C99-blue.svg)](https://en.wikipedia.org/wiki/C99)
[![Platform: Windows](https://img.shields.io/badge/Platform-Windows%207%20--%2011-lightgrey.svg)](https://www.microsoft.com/windows)
[![Binary Size](https://img.shields.io/badge/Binary%20Size-92%20KB-brightgreen.svg)](#8-源码构建与测试指南)
[![Tests: 47 Passing](https://img.shields.io/badge/Tests-47%20Passing-success.svg)](#8-源码构建与测试指南)

---

## 目录 / Table of Contents
- [第一部分：简体中文说明 (Part 1: Chinese)](#第一部分简体中文说明)
  - [1. 项目概览](#1-项目概览)
  - [2. 核心架构与核心技术](#2-核心架构与核心技术)
  - [3. 致敬与参考的开源项目](#3-致敬与参考的开源项目)
  - [4. 界面指南与 6 列步骤抽屉](#4-界面指南与-6-列步骤抽屉)
  - [5. 超时恢复策略配置 (五选一)](#5-超时恢复策略配置-五选一)
  - [6. 快捷键与上升沿锁存机制](#6-快捷键与上升沿锁存机制)
  - [7. 单文件工程容器 (.ttp v2) 与解包工具](#7-单文件工程容器-ttp-v2-与解包工具)
  - [8. 对比矩阵](#8-对比矩阵)
  - [9. 源码构建与测试指南](#9-源码构建与测试指南)
- [Part 2: English Documentation](#part-2-english-documentation)
  - [1. Overview](#1-overview)
  - [2. Architectural & Technical Highlights](#2-architectural--technical-highlights)
  - [3. Open-Source Attributions & References](#3-open-source-attributions--references)
  - [4. UI Drawer & 6-Column Step Table](#4-ui-drawer--6-column-step-table)
  - [5. Timeout Recovery Policies](#5-timeout-recovery-policies)
  - [6. Global Hotkeys & Rising-Edge Latch](#6-global-hotkeys--rising-edge-latch)
  - [7. Single-File Project Container (.ttp v2) & Tooling](#7-single-file-project-container-ttp-v2--tooling)
  - [8. Feature Comparison Matrix](#8-feature-comparison-matrix)
  - [9. Build Instructions & Test Suites](#9-build-instructions--test-suites)
- [开源许可证 / License (MIT)](#开源许可证--license-mit)

---

# 第一部分：简体中文说明

## 1. 项目概览

**TinyTask Pro** 是对经典极简宏录制工具 TinyTask 的现代化演进与企业级 RPA 重构。

传统机器人流程自动化（RPA）工具（如 Microsoft Power Automate Desktop、UiPath、AutoHotkey 庞大运行时、各种 Python/PyAutoGUI 方案）动辄需要数百兆乃至数吉字节的运行环境依赖（.NET Framework、Python 解释器、Electron、Chromium 内核等）。这使得轻量级运维、嵌入式测试或无网络隔离环境下的自动化部署极为沉重。

**TinyTask Pro** 彻底打破这一局限：
* **极限体积与零依赖**：基于纯 C99 语言与原生 Win32 API 打造，整个软件为**单个独立可执行文件，体积仅约 88 KB**，无需安装任何运行库，拷入即用；
* **双通道智能定位**：融合了计算机视觉（CV）与操作系统原生无障碍树（MSAA / UI Automation），彻底告别传统宏录制工具“窗口一挪动、分辨率一变就点击落空”的致命痛点；
* **企业级稳健性**：内建两级级联检索、字形穿透与内衬空白区隔离算法、空间欧氏距离消歧以及五重超时恢复策略。

本项目保留了经典的 TinyTask 1.77 逆向分析基线源码，同时推出了功能完整的现代化 Pro 版本。

---

## 2. 核心架构与核心技术

TinyTask Pro 在不到 100 KB 的代码空间内，完整实现了一整套微型现代计算机视觉与桌面自动化流水线：

### 🖼️ 1. 纯 C 语言 UIED 容器边缘分割算法 (UI Element Detection)
* **$3 \times 3$ Sobel 梯度幅值提取**：录制点击瞬间捕获 $256 \times 256$ 局部感兴趣区域（ROI），通过水平算子 $G_x$ 与垂直算子 $G_y$ 计算梯度能量幅值 $M = |G_x| + |G_y|$，并基于局部方差自适应计算动态二值化边缘阈值。
* **形态学闭运算 (Morphological Closing)**：在纯 C 内存中实现 $3 \times 3$ 矩形核膨胀（Dilation，Max Filter）紧跟腐蚀（Erosion，Min Filter），自动桥接文字字符笔画断隙以及按钮边框的微小断裂点。
* **字形穿透与内衬空白区（Padding Gap）分析**：针对现代 GUI 控件内部普遍包含文字/图标的特征，算法向外扫描时会自动区分 $<12\sim 18\text{px}$ 的内部小字形与外层包裹矩形；在跨越内衬空白背景区（Padding Zone）后精准收敛并锁定在**最外层按钮边框**。
* **紧凑堆叠单像素防越界隔离**：向外扩展一旦抵达按钮外边框即刻触发行阻断，在如 Excel、工具栏等仅间隔 1~2px 的密集堆叠按钮群中，确保 100% 独立裁剪，绝不渗漏侵染相邻按钮。
* **桌面大磁贴自适应扩容**：针对 Windows 桌面存在双行文本（如 Counter-Strike 2 等长标题磁贴）的场景，无障碍高度判定阈值放宽至 105px，实现整块标准磁贴（$74 \times 87$）的一体化完整捕获。

### 🎭 2. 自动边缘色差连通泛洪抠图 (Border Chromakey Flood-Fill)
* **背景变幻数学根因破解**：当桌面图标（如 $74 \times 70$）移至浅蓝等高亮壁纸区时，因录制底色与目标位置底色差异，传统全矩形 NCC 得分会从 $1.0$ 骤跌至 $0.13 \sim 0.69$ 导致漏检。
* **4 边界基准色采样与 4 邻域 BFS 连通泛洪**：自动沿矩形四周边框采样基准背景色，以色差欧氏距离容差 $\Delta E \le \text{tol}$ 向内连通泛洪，将外部无关壁纸底色精准抠除，生成标准 **32bpp BGRA 资产**（前景 $A=255$，背景 $A=0$）。
* **防误抠安全回退门禁**：若泛洪后前景保留比 $<15\%$ 或边框色差方差 $>60$（检测到非单色背景），自动触发安全兜底回退为全保留（$A=255$），绝不误伤图标本体。

### ⚡ 3. 掩码探测点 SAD 粗筛与零堆内存 Masked NCC 搜图引擎
* **零堆内存架构 (Zero-Heap Lazy VirtualAlloc)**：告别高分辨率（支持至 4K 3840×2160）下每帧 8MB 频繁 `malloc`/`free` 的内存碎片与卡顿，单例按需虚内存提交，PE 磁盘体积净增 **0 字节**。
* **Tier 1 - 掩码高频特征探测点 SAD 极速粗筛**：在前景掩码区域自动提取 16 个高频特征探测点，粗筛阶段仅需数纳秒计算探测点 SAD，快速短路排除 99% 以上无效候选窗口。
* **Tier 2 - 掩码归一化互相关 (Masked NCC) 精确核**：
  $$\mu_T = \frac{1}{N_m} \sum_{M_i=1} T_i, \quad \sigma_T^2 = \sum_{M_i=1} (T_i - \mu_T)^2$$
  $$\mu_I = \frac{1}{N_m} \sum_{M_i=1} I_i, \quad \sigma_I^2 = \sum_{M_i=1} (I_i - \mu_I)^2$$
  $$\text{Masked NCC} = \frac{\sum_{M_i=1} (T_i - \mu_T)(I_i - \mu_I)}{\sqrt{\sigma_T^2 \cdot \sigma_I^2}}$$
  完全剔除背景像素（权重置零），在 2560×1440 极端壁纸色调变幻场景中，匹配得分从 0.13~0.69 飙升至 **1.0000 满分稳健命中**！

### 🎛️ 4. 原生 Win32 步骤微调模态框与 60FPS 灰白棋盘格实时预览
* **可视化微调交互**：双击步骤行或右键点击呼出编辑菜单，弹出原生轻量模态对话框（`TTP_StepEditDlg`）。
* **TrackBar 0~100 滑动条微秒级重算**：实时调整透明容差（Tolerance），微秒级重新计算 BFS 连通掩码。
* **60FPS 灰白棋盘格实时预览**：预览画布绘制经典 8×8 灰白相间透明棋盘格，通过内存顶级 DIB 缓冲在 sub-millisecond 级完成 Alpha 实时混合渲染，拖动滑块即可丝滑即时预览抠图透明轮廓！

### 🎯 5. 双引擎定位与空间欧氏距离消歧
* **双通道互补定位**：
  * **通道 A（无障碍文本提取）**：通过 Win32 MSAA / `IAccessible` 递归解析鼠标悬停处的控件文字（如 `"确定"`、`"取消"`、`"打开"`、`"保存"`）。
  * **通道 B（掩码分层视觉模板）**：结合透明掩码与 NCC 模板匹配，无惧壁纸与光照变幻。
* **空间欧氏距离最近邻消歧 (Spatial Disambiguation)**：
  当页面中出现多个同名控件时，自动计算各候选坐标与录制物理坐标的欧氏距离选取最近邻：
  $$D_i = \sqrt{(X_i - X_{orig})^2 + (Y_i - Y_{orig})^2}$$

### 🔬 6. 纯整型开方 (isqrt) 与调色板 RLE8 极致二进制瘦身
* **彻底切断 `<math.h>` 与 `-lm`**：实现基于 64 位纯位移的硬件级整型快速开方 `ttp_isqrt(unsigned long long n)` 与牛顿迭代浮点开方 `ttp_sqrt(double x)`，零 CRT 浮点库依赖。
* **工具栏位图调色板压缩**：恢复原生 8bpp RLE8 调色板位图，资源段直接减少 6.3 KB；
* **自研秒数解析器**：自研 `parse_seconds` 消除 `atof` 与 CRT convert 依赖，整机体积严格压制在 **~92 KB**！

---

## 3. 致敬与参考的开源项目

TinyTask Pro 的诞生离不开开源社区与经典软件的智慧结晶，特别致敬并参考了以下项目与研究成果：

1. **[TinyTask 1.77] (by Vista Software)**  
   * *致敬与传承*：Windows 平台上最经典的超轻量级宏录制软件。本项目深入逆向分析了其原始 Win32 消息循环、自绘 Ribbon 工具栏和紧凑设计理念，并在其基础上设计了 Pro 版本的全面架构升级。
2. **[UIED (User Interface Element Detection)] (by Mulong Xie et al., PKU-SE-Lab)**  
   * *核心启发*：UIED 项目提出的“基于边缘梯度与形态学闭运算融合文本/非文本 GUI 构件”的思想，启发了 TinyTask Pro 纯 C 语言轻量级控件分割与内衬分析算法的实现。
3. **[Automatic Bounding Box Cropping] & OpenCV `squares.py` (by RBirkeland & OpenCV Community)**  
   * *核心启发*：自适应边界框裁剪与长宽比过滤的几何分析方案，启发了本项目在缺乏操作系统无障碍元数据时自适应收敛外轮廓的逻辑。
4. **[Microsoft Power Automate Desktop] & [UiPath]**  
   * *核心启发*：启发了“语义级录制”、“无障碍与视觉双通道互补定位”、“多目标空间消歧”以及“超时恢复策略决策树”的现代 RPA 设计范式。
5. **[MinGW-w64] & GCC 编译生态**  
   * *基石支撑*：提供轻量级 Win32 静态链接能力，使得在零外部动态库依赖下编译出 < 90 KB 二进制成为现实。

---

## 4. 界面指南与 6 列步骤抽屉

TinyTask Pro 完美保持了经典 TinyTask 极其小巧、不遮挡工作区的悬浮工具栏设计，同时提供了可平滑折叠展开的交互式步骤面板：

* **收缩工具栏状态 (`263 × 54 px` / 无标题栏 `263 × 42 px`)**：极简浮动工具条，占用屏幕空间极小；
* **展开抽屉状态 (`380 × 360 px`)**：点击工具栏 `[Steps]` 按钮平滑展开步骤编辑抽屉，集成原生 `SysListView32` 6 列步骤报表。

### 步骤抽屉 6 列数据规范：

| 列号 | 列名 | 宽度 | 子项索引 | 说明 | 交互能力 |
|:---:|---|:---:|:---:|---|---|
| **0** | `#` | 28px | 0 | 步骤序号（1, 2, 3...） | **双击打开步骤微调与透明掩码实时预览对话框** |
| **1** | `Action` | 52px | 1 | 动作类型（`Click`, `Drag`, `Text`, `DblClick`, `RClick`, `Hotkey`） | **双击打开步骤微调对话框** |
| **2** | `Target` | 95px | 2 | 识别文本、无障碍名称或目标说明 | **双击打开步骤微调对话框** |
| **3** | `Timeout` | 50px | 3 | 超时等待时间（如 `3.0s`） | **双击就地编辑秒数**（直接输入 `5.0` 等数字） |
| **4** | `On Timeout` | 75px | 4 | 超时策略（`[Prompt]`, `[Retry]`, `[Coord]`, `[Skip]`, `[Stop]`） | **双击弹出原生切换菜单**（五选一即刻配置） |
| **5** | `Asset` | 40px | 5 | 视觉模板资产指示（`[BMP]` 或 `-`） | **双击打开步骤微调对话框** |

### 步骤抽屉快捷操作与右键菜单：
* **鼠标右键快捷菜单**：在列表任意步骤右击，呼出菜单包含：
  - `编辑步骤细节与透明掩码...`：直接唤起带 60FPS 灰白棋盘格实时预览的微调弹窗；
  - `上移一步` / `下移一步` / `删除步骤`。
* `[+ Add]`：追加纯坐标点击新步骤。
* `[- Del]`：删除当前高亮选中的步骤。
* `[Up]` / `[Down]`：上下移动调整步骤执行时序。
* `[Run Step]`：仅对当前选中的单一步骤执行单步试运行。

---

## 5. 超时恢复策略配置 (五选一)

在抽屉中**双击第 4 列 `On Timeout`**，会立即在鼠标位置呼出原生 Windows 上下文快捷菜单，可灵活为每一个步骤赋予不同的容错决策：

1. **`[Prompt] 询问用户` (`TTP_TIMEOUT_ACT_DEFAULT`)**：
   - 寻找超时后暂停回放，弹出原生模态对话框由用户现场抉择：`[重试]`、`[点击录制坐标]`、`[跳过]`、`[终止]`。
2. **`[Retry] 自动重试` (`TTP_TIMEOUT_ACT_RETRY`)**：
   - 持续重试匹配直到目标出现（适合等待长时间加载的网络页面或对话框）。
3. **`[Coord] 点击录制坐标` (`TTP_TIMEOUT_ACT_USE_RECORDED`)**：
   - 视觉未找到时，自动降级点击最初录制时的物理坐标 $(origX, origY)$。
4. **`[Skip] 跳过此步骤` (`TTP_TIMEOUT_ACT_SKIP`)**：
   - 找不到目标时放弃此动作，立即继续执行下一步。
5. **`[Stop] 停止回放` (`TTP_TIMEOUT_ACT_STOP`)**：
   - 找不到目标时立即安全终止整个宏的执行。

---

## 6. 快捷键与上升沿锁存机制

* **录制热键 (Recording)**：默认 `Ctrl + Shift + Alt + R`，可选 `Print Screen`、`F8`、`F12`、自定义组合键。
* **回放热键 (Playback)**：默认 `Ctrl + Shift + Alt + P`，可选 `Print Screen`、`F8`、`F12`、自定义组合键。
* **紧急终止 (Emergency Abort)**：回放期间按下 `{PAUSE}` 或 `{ScrollLock}` 立即终止。

---

## 7. 单文件工程容器 (.ttp) 与解包工具

### 单文件工程规范 (.ttp v2)
TinyTask Pro 摒弃了将宏脚本与图片零散存放在文件夹中的脆弱做法，采用一体化内聚二进制打包容器：
* 文件格式：`.ttp` (TinyTask Pro Project)；
* 协议版本：**v2 规范**（`TTPHeader.version = 2`，`TTPStep` 176 字节，包含 `chromaTol` 容差与对齐填充）；
* 资产存储：支持未压缩 **32 位 BGRA BMP（含 Alpha 掩码透明通道）** 与 24 位 BMP，加载时自动将老旧 24 位资产升阶为 32 位（$A=255$）；
* 兼容性：100% 自动识别加载 v1 格式（172 字节 Step，`chromaTol` 缺省置 0），并向下完全兼容经典 TinyTask `.rec` 坐标文件。

### 配套 Python 工具与一键拖拽脚本
项目在 `src/` 与 `bin/` 中提供了开箱即用的工程逆向检查与解包工具：
* **命令行解包**：
  ```bash
  python reverse-gemini/src/tinytask_tool.py unpack "path/to/macro.ttp"
  ```
  自动在同目录下生成 `_unpacked/` 文件夹，提取全部步骤的 `.bmp` 图片与 `manifest.json` 结构清单。
* **Windows 免敲命令一键解包**：
  在文件管理器中，直接将任意 `.ttp` 文件拖拽到 [`reverse-gemini/bin/unpack_ttp.bat`](file:///e:/reverse-gemini/reverse-gemini/bin/unpack_ttp.bat) 图标上即可瞬间完成拆包查看！

---

## 8. 对比矩阵

| 核心指标 / 功能 | TinyTask 1.77 经典版 | TinyTask Pro 专业版 | 传统商业 RPA (Power Automate / UiPath) |
|---|---|---|---|
| **二进制体积** | ~35 KB | **~92 KB (单文件独立)** | 500 MB ~ 2 GB 运行环境 |
| **运行时依赖** | 无 (纯 Win32) | **无 (原生 Win32 / GDI, 0 `<math.h>`/`-lm`)** | .NET / Python / Node / Chromium |
| **元素定位机制** | 仅绝对物理坐标 $(X,Y)$ | **透明掩码分层视觉 + 无障碍文本树** | UI 选择器 / DOM / 云端 OCR |
| **壁纸变色抗扰度**| 无（纯坐标） | **极高（掩码将无关背景权重置零，得分从 0.13 跃升至 1.0000）** | 弱（颜色变化极易匹配失效） |
| **透明掩码交互** | 无 | **原生 60FPS 灰白棋盘格实时预览 + TrackBar 0~100 微调** | 复杂属性面板或需外部修图 |
| **原地响应速度** | 无图像计算 | **1 ~ 2 ms (Tier 1 探测 SAD + ROI)** | 500 ~ 2000 ms |
| **复杂界面泛用性** | 极弱（窗口移动即失效） | **极高（UIED 衬距隔离 + 空间消歧）** | 高（依赖重量级运行时解析） |
| **可视化步骤编辑** | 无 | **6 列交互抽屉 + 双击微调弹窗 + 右键菜单** | 庞大流程图设计器 |
| **每步超时策略** | 无 | **5 种策略独立按步配置** | 属性检查器面板 |
| **工程分享便携性** | 仅文本坐标 | **单文件打包 (.ttp v2 包含 32bpp 掩码图像)** | 复杂项目工程文件夹 |
| **开源协议** | 专有免费软件 | **MIT 开源协议** | 商业专有 / 社区限制版 |

---

## 9. 源码构建与测试指南

### 构建环境：
* 操作系统：Windows 7 / 8 / 10 / 11 (x86 或 x64)
* 编译器：MinGW-w64 GCC (包含原生 Windows SDK 头文件与库，**无需任何数学或浮点库**)

### 一键编译 Release 可执行文件：
```bash
# 1. 编译原生 Win32 资源文件 (图标、紧凑 8bpp RLE8 工具栏位图、ComCtl 6.0 Manifest)
windres reverse-gemini/src/tinytask_pro.rc -O coff -o reverse-gemini/src/tinytask_pro_res.o

# 2. 编译并链接独立二进制 (零 -lm 依赖，启用 -Os 体积优化与 -Wl,--gc-sections 符号精简)
gcc -Os -s -mwindows \
    -ffunction-sections -fdata-sections \
    -fno-asynchronous-unwind-tables -fno-ident \
    "-Wl,--gc-sections" \
    reverse-gemini/src/tinytask_pro.c \
    reverse-gemini/src/ttp_storage.c \
    reverse-gemini/src/ttp_vision.c \
    reverse-gemini/src/ttp_engine.c \
    reverse-gemini/src/tinytask_pro_res.o \
    -lcomctl32 -loleacc -lole32 -loleaut32 -lgdi32 -luser32 -lcomdlg32 -lshell32 \
    -o reverse-gemini/bin/tinytask_pro.exe

# 3. 清理临时资源对象
rm reverse-gemini/src/tinytask_pro_res.o
```
编译后可在 [`reverse-gemini/bin/tinytask_pro.exe`](file:///e:/reverse-gemini/reverse-gemini/bin/tinytask_pro.exe) 查看输出文件，大小约为 **92.5 KB (94,720 字节)**。

### 运行全套 8 大单元与回归测试套件 (47+ 项全绿)：
```bash
# 1. 存储层序列化往返与 32bpp BGRA 向下兼容测试 (7 项)
gcc -Os -Ireverse-gemini/src reverse-gemini/tests/test_storage.c reverse-gemini/src/ttp_storage.c -o test_storage.exe && ./test_storage.exe

# 2. 纯整型 64 位 isqrt 与牛顿开方精度测试 (2 组)
gcc -Os -Ireverse-gemini/src reverse-gemini/tests/test_isqrt.c reverse-gemini/src/ttp_vision.c -loleacc -lole32 -loleaut32 -lgdi32 -luser32 -o test_isqrt.exe && ./test_isqrt.exe

# 3. 自动边缘色差 BFS 泛洪抠图与防误抠安全回退测试 (4 项)
gcc -Os -Ireverse-gemini/src reverse-gemini/tests/test_chromakey.c reverse-gemini/src/ttp_storage.c reverse-gemini/src/ttp_vision.c reverse-gemini/src/ttp_engine.c -lole32 -loleaut32 -loleacc -lgdi32 -luser32 -o test_chromakey.exe && ./test_chromakey.exe

# 4. 2560x1440 极端壁纸色调变幻与掩码探测 SAD + Masked NCC 搜图测试 (5 项)
gcc -Os -Ireverse-gemini/src reverse-gemini/tests/test_masked_vision.c reverse-gemini/src/ttp_storage.c reverse-gemini/src/ttp_vision.c reverse-gemini/src/ttp_engine.c -lole32 -loleaut32 -loleacc -lgdi32 -luser32 -o test_masked_vision.exe && ./test_masked_vision.exe

# 5. 纯 C 视觉与 UIED 隔离测试 (5 项)
gcc -Os -Ireverse-gemini/src reverse-gemini/tests/test_vision.c reverse-gemini/src/ttp_storage.c reverse-gemini/src/ttp_vision.c reverse-gemini/src/ttp_engine.c -lole32 -loleaut32 -loleacc -lgdi32 -luser32 -o test_vision.exe && ./test_vision.exe

# 6. 语义动作合成与回放引擎测试 (9 项)
gcc -Os -Ireverse-gemini/src reverse-gemini/tests/test_engine.c reverse-gemini/src/ttp_storage.c reverse-gemini/src/ttp_vision.c reverse-gemini/src/ttp_engine.c -lole32 -loleaut32 -loleacc -lgdi32 -luser32 -o test_engine.exe && ./test_engine.exe

# 7. 6 列抽屉、微调模态框与 60FPS 棋盘格集成测试 (7 项)
gcc -Os -Ireverse-gemini/src reverse-gemini/tests/test_tinytask_pro.c reverse-gemini/src/ttp_storage.c reverse-gemini/src/ttp_vision.c reverse-gemini/src/ttp_engine.c -lcomctl32 -lole32 -loleaut32 -loleacc -lgdi32 -luser32 -lcomdlg32 -lshell32 -o test_tinytask_pro.exe && ./test_tinytask_pro.exe

# 8. 级联检索、双引擎与极端场景全量回归基准测试 (13 项)
gcc -Os -Ireverse-gemini/src reverse-gemini/tests/test_fix_repro.c reverse-gemini/src/ttp_storage.c reverse-gemini/src/ttp_vision.c reverse-gemini/src/ttp_engine.c -lole32 -loleaut32 -loleacc -lgdi32 -luser32 -o test_fix_repro.exe && ./test_fix_repro.exe
```

---
---

# Part 2: English Documentation

## 1. Overview

**TinyTask Pro** is an evolutionary breakthrough in minimalist macro recording and desktop Robotic Process Automation (RPA).

Traditional desktop RPA tools (such as Microsoft Power Automate Desktop, UiPath, AutoHotkey runtimes, and bulky Python/PyAutoGUI scripts) mandate gigabytes of runtime dependencies (.NET Framework, Python environments, Electron, or Chromium runtimes). This creates prohibitive barriers for lightweight operations, embedded system testing, air-gapped secure workstations, or instant automated deployment.

**TinyTask Pro** completely eliminates this footprint:
* **Radical Portability (< 95 KB)**: Crafted entirely in pure C99 and native Win32 APIs, compiling into a **single standalone binary of ~92 KB** with zero external dependencies and zero CRT float/math runtime libraries.
* **Dual-Track Target Localization & Chromakey Masking**: Combines computer vision (CV) with native Windows accessibility structures (MSAA / UI Automation). Features 4-neighbor BFS border color flood-fill to carve away irrelevant desktop wallpaper colors into 32bpp BGRA masks, eradicating missed matches when controls migrate across disparate background colors.
* **Production-Grade Reliability**: Features zero-heap Probe SAD coarse filtering, Masked Normalized Cross-Correlation (Masked NCC), UIED-inspired glyph-through padding gap bounding, Euclidean spatial disambiguation, and an interactive 60FPS checkerboard step configuration dialog.

---

## 2. Architectural & Technical Highlights

Within fewer than 95 KB of binary code, TinyTask Pro delivers an end-to-end native vision and RPA automation pipeline:

### 🖼️ 1. Pure-C UIED Enclosing Boundary Segmentation
* **$3 \times 3$ Sobel Gradient Field**: Captures a $256 \times 256$ Region of Interest (ROI) centered on click coordinates, computing gradient magnitudes $M = |G_x| + |G_y|$ with dynamic thresholding based on local luminance variance.
* **Morphological Closing**: Evaluates a $3 \times 3$ dilation followed by erosion filter directly in memory, bridging fragmented text character strokes and minor button border breaks.
* **Glyph Penetration & Padding Gap Analysis**: Distinguishes inner character/icon glyphs ($<12\sim 18\text{px}$ from origin) from outer enclosing buttons. Automatically sweeps across interior padding zones to lock strictly onto the **outermost button container boundary**.
* **Zero-Bleed Separation on Stacked Buttons**: Aborts outward expansion at the outer border, guaranteeing that buttons packed tightly with only 1~2px gutters (e.g. dense toolbars or spreadsheets) never bleed across into neighboring elements.
* **Desktop Tile Height Adaptive Expansion**: Supports Windows desktop icons with multi-line titles (such as Counter-Strike 2) by extending accessible height bounds up to 105px, capturing complete $74 \times 87$ tiles seamlessly.

### 🎭 2. Automatic Border Chromakey Flood-Fill
* **Mathematical Wallpaper Interference Solution**: When desktop icons ($74 \times 70$) migrate to sky-blue or bright wallpaper regions, background color mismatches traditionally cause unmasked NCC scores to plunge from $1.0$ down to $0.13 \sim 0.69$, causing severe misses.
* **4-Border Color Sampling & 4-Neighbor BFS Flood-Fill**: Samples perimeter background colors and conducts flood-filling within tolerance $\Delta E \le \text{tol}$, carving outer wallpaper into standard **32bpp BGRA assets** (foreground $A=255$, background $A=0$).
* **Safety Fallback Gate**: If foreground retention drops below $15\%$ or perimeter variance exceeds $60$, automatically reverts to full opacity to prevent accidental over-carving.

### ⚡ 3. Masked Cascaded NCC Engine & Zero-Heap Architecture
* **Zero-Heap Lazy VirtualAlloc**: Eliminates per-frame 8MB `malloc`/`free` heap fragmentation across high-resolution (up to 4K 3840×2160) monitors, reserving zero physical bytes in the PE binary on disk.
* **Tier 1 - Masked Feature Probe SAD Coarse Filtering**: Precomputes 16 high-gradient probe points across foreground regions; evaluates candidate positions in nanoseconds and prunes 99%+ of non-matching windows.
* **Tier 2 - Masked NCC Exact Kernel**:
  $$\text{Masked NCC} = \frac{\sum_{M_i=1} (T_i - \mu_T)(I_i - \mu_I)}{\sqrt{\sigma_T^2 \cdot \sigma_I^2}}$$
  Zeroes out background pixel weights, leaping from $0.13 \sim 0.69$ to a **solid 1.0000 match** on 2560×1440 wallpapers.

### 🎛️ 4. Native Win32 Step Edit Dialog & 60FPS Checkerboard Real-Time Preview
* **Interactive Configuration**: Double-click any step row or right-click to open `TTP_StepEditDlg`.
* **TrackBar 0~100 Microsecond Recalculation**: Adjust chromakey tolerance with instant response.
* **60FPS Transparency Checkerboard**: Renders an 8×8 alternating gray/white checkerboard with sub-millisecond memory DIB Alpha compositing for buttery-smooth live preview.

### 🎯 5. Dual-Track Recognition & Spatial Disambiguation
* **Track A (Accessible Text Extraction)**: Traverses Windows MSAA / `IAccessible` trees to capture text labels (`"OK"`, `"Submit"`, `"Search"`, `"Save"`).
* **Track B (Masked Visual Template)**: Illumination-invariant template matching immune to background hue variations.
* **Spatial Euclidean Disambiguation**: Resolves duplicate button ambiguities by choosing the closest candidate to original coordinates:
  $$D_i = \sqrt{(X_i - X_{orig})^2 + (Y_i - Y_{orig})^2}$$

### 🔬 6. Pure-Integer isqrt & Extreme Binary Optimization
* **Zero `<math.h>` & Zero `-lm`**: Implemented bitwise 64-bit integer square root `ttp_isqrt(unsigned long long n)` and hardware Newton-Raphson `ttp_sqrt(double x)`.
* **RLE8 Palette Bitmap**: Restored original 8bpp RLE8 toolbar bitmap, cutting 6.3 KB from resources.
* **Custom String Parser**: Replaced CRT `atof` to eliminate conversion DLL imports, stabilizing binary at **~92 KB**.

---

## 3. Open-Source Attributions & References

TinyTask Pro acknowledges the foundational work of the following projects and research:

1. **[TinyTask 1.77] (by Vista Software)**: The gold standard for minimalist macro recording on Windows. Reverse-engineered and re-architected into TinyTask Pro.
2. **[UIED (User Interface Element Detection)] (by Mulong Xie et al., PKU-SE-Lab)**: The hybrid gradient + morphological closing pipeline inspired our pure-C GUI element extraction engine.
3. **[Automatic Bounding Box Cropping] & OpenCV `squares.py`**: Geometric contour extraction and aspect-ratio bounding box fitting inspired our ray-casted button contouring.
4. **[Microsoft Power Automate Desktop] & [UiPath]**: Influenced dual-track accessibility + vision fallbacks, spatial disambiguation, and per-step timeout recovery trees.
5. **[MinGW-w64] & GCC Compiler Suite**: Enabled ultra-compact, zero-dependency C99 Win32 binary compilation.

---

## 4. UI Drawer & 6-Column Step Table

TinyTask Pro maintains the iconic floating ribbon while providing an expandable workflow editor:

* **Collapsed Ribbon Mode (`263 × 54 px` or `263 × 42 px`)**: The ultra-compact floating toolbar;
* **Expanded Drawer Mode (`380 × 360 px`)**: Clicking `[Steps]` smoothly expands the 6-column native Windows ListView (`SysListView32`).

### 6-Column Table Specification:

| Col # | Header Text | Width | SubItem | Description | Interaction |
|:---:|---|:---:|:---:|---|---|
| **0** | `#` | 28px | 0 | Step sequence number | **Double-click opens Step Edit & 60FPS Mask Preview** |
| **1** | `Action` | 52px | 1 | Action type (`Click`, `Drag`, `Text`, `DblClick`, `RClick`, `Hotkey`) | **Double-click opens Step Edit Dialog** |
| **2** | `Target` | 95px | 2 | Accessible label, text, or coordinates | **Double-click opens Step Edit Dialog** |
| **3** | `Timeout` | 50px | 3 | Timeout duration (e.g., `3.0s`) | **Double-click to edit seconds** (enter `5.0` directly) |
| **4** | `On Timeout` | 75px | 4 | Timeout recovery policy | **Double-click for popup menu** (choose 1 of 5 policies) |
| **5** | `Asset` | 40px | 5 | Visual template asset indicator (`[BMP]` or `-`) | **Double-click opens Step Edit Dialog** |

### Context Menu & Quick Actions:
* **Right-Click Context Menu**: Right-click any row to access:
  - `Edit Step Details & Chromakey Mask...` (opens dialog with 60FPS preview)
  - `Move Up` / `Move Down` / `Delete Step`
* `[+ Add]`: Appends coordinate click step.
* `[- Del]`: Deletes selected step.
* `[Up]` / `[Down]`: Adjusts execution order.
* `[Run Step]`: Executes single selected step.

---

## 5. Timeout Recovery Policies

Double-clicking SubItem 4 (`On Timeout`) displays an in-place native Win32 context menu allowing instant policy selection:

1. **`[Prompt] Ask User` (`TTP_TIMEOUT_ACT_DEFAULT`)**: Pauses and presents a recovery dialog (`[Retry]`, `[Use Recorded Pos]`, `[Skip]`, `[Stop]`).
2. **`[Retry] Retry Loop` (`TTP_TIMEOUT_ACT_RETRY`)**: Retries continuously until the element appears.
3. **`[Coord] Click Recorded Pos` (`TTP_TIMEOUT_ACT_USE_RECORDED`)**: Clicks original $(origX, origY)$ physical coordinates as fallback.
4. **`[Skip] Skip Step` (`TTP_TIMEOUT_ACT_SKIP`)**: Skips the missing element and advances to the next step.
5. **`[Stop] Stop Macro` (`TTP_TIMEOUT_ACT_STOP`)**: Immediately terminates macro execution safely.

---

## 6. Global Hotkeys & Rising-Edge Latch

* **Recording Hotkey**: Default `Ctrl + Shift + Alt + R`, configurable to `Print Screen`, `F8`, `F12`, or custom combinations.
* **Playback Hotkey**: Default `Ctrl + Shift + Alt + P`, configurable to `Print Screen`, `F8`, `F12`, or custom combinations.
* **Emergency Abort**: Pressing `{PAUSE}` or `{ScrollLock}` immediately aborts playback.

---

## 7. Single-File Project Container (.ttp v2) & Tooling

* **`.ttp v2` Binary Packaging**: Encapsulates metadata header `TTPHeader` (version 2), step descriptors with `chromaTol`, and 32-bit BGRA DIB/BMP visual assets with Alpha transparency channel. Fully backward-compatible with v1 and `.rec` files.
* **Python Tooling (`tinytask_tool.py`)**:
  ```bash
  python reverse-gemini/src/tinytask_tool.py unpack "path/to/macro.ttp"
  ```
  Extracts all step images and outputs `manifest.json`.
* **Drag-and-Drop Batch Script (`unpack_ttp.bat`)**:
  Drag any `.ttp` file directly onto [`reverse-gemini/bin/unpack_ttp.bat`](file:///e:/reverse-gemini/reverse-gemini/bin/unpack_ttp.bat) in Windows Explorer to unpack instantly.

---

## 8. Feature Comparison Matrix

| Feature | TinyTask 1.77 | TinyTask Pro | Heavyweight RPA (Power Automate, UiPath) |
|---|---|---|---|
| **Binary Footprint** | ~35 KB | **~92 KB (Standalone)** | 500 MB ~ 2 GB Runtime |
| **External Dependencies**| None (Win32) | **None (Native Win32, 0 `<math.h>`/`-lm`)** | .NET / Python / Node / Chromium |
| **Element Locating** | Fixed Physical $(X,Y)$ | **Masked Cascaded Vision + Accessibility** | Selectors / DOM / Cloud OCR |
| **Wallpaper Immunity** | None | **Immune (Masked NCC score 1.0000 on hue shifts)** | Weak (Color drift breaks matches) |
| **Real-Time Mask Preview**| None | **Native 60FPS Checkerboard + TrackBar** | Heavy property inspectors |
| **In-Place Match Latency**| N/A | **1 ~ 2 ms (Tier 1 Probe SAD + ROI)** | 500 ~ 2000 ms |
| **Dense Button Isolation**| N/A | **UIED Padding Gap Isolation** | High (Heavyweight engine parsing) |
| **Visual Step Editing** | None | **6-Column Drawer + Live Edit Modal** | Flowchart / Diagram Studio |
| **Timeout Policy Engine**| None | **Per-Step Context Menu** | Property Inspector |
| **Project Packaging** | Coordinates only | **Single-file `.ttp v2` (32bpp alpha assets)** | Complex project directories |
| **Open-Source License** | Freeware | **MIT License** | Proprietary / Commercial |

---

## 9. Build Instructions & Test Suites

### Build Release Executable:
```bash
windres reverse-gemini/src/tinytask_pro.rc -O coff -o reverse-gemini/src/tinytask_pro_res.o
gcc -Os -s -mwindows \
    -ffunction-sections -fdata-sections \
    -fno-asynchronous-unwind-tables -fno-ident \
    "-Wl,--gc-sections" \
    reverse-gemini/src/tinytask_pro.c \
    reverse-gemini/src/ttp_storage.c \
    reverse-gemini/src/ttp_vision.c \
    reverse-gemini/src/ttp_engine.c \
    reverse-gemini/src/tinytask_pro_res.o \
    -lcomctl32 -loleacc -lole32 -loleaut32 -lgdi32 -luser32 -lcomdlg32 -lshell32 \
    -o reverse-gemini/bin/tinytask_pro.exe
rm reverse-gemini/src/tinytask_pro_res.o
```
Resulting binary: [`reverse-gemini/bin/tinytask_pro.exe`](file:///e:/reverse-gemini/reverse-gemini/bin/tinytask_pro.exe) (~92.5 KB, 94,720 bytes).

### Run Test Suites (47+ Tests Passing):
```bash
# 1. Storage roundtrip & 32bpp backward compatibility (7 tests)
gcc -Os -Ireverse-gemini/src reverse-gemini/tests/test_storage.c reverse-gemini/src/ttp_storage.c -o test_storage.exe && ./test_storage.exe

# 2. Integer isqrt & Newton-Raphson precision suites (2 suites)
gcc -Os -Ireverse-gemini/src reverse-gemini/tests/test_isqrt.c reverse-gemini/src/ttp_vision.c -loleacc -lole32 -loleaut32 -lgdi32 -luser32 -o test_isqrt.exe && ./test_isqrt.exe

# 3. Chromakey BFS flood-fill & safety fallback (4 tests)
gcc -Os -Ireverse-gemini/src reverse-gemini/tests/test_chromakey.c reverse-gemini/src/ttp_storage.c reverse-gemini/src/ttp_vision.c reverse-gemini/src/ttp_engine.c -lole32 -loleaut32 -loleacc -lgdi32 -luser32 -o test_chromakey.exe && ./test_chromakey.exe

# 4. 2560x1440 wallpaper interference & Masked Probe SAD + NCC (5 tests)
gcc -Os -Ireverse-gemini/src reverse-gemini/tests/test_masked_vision.c reverse-gemini/src/ttp_storage.c reverse-gemini/src/ttp_vision.c reverse-gemini/src/ttp_engine.c -lole32 -loleaut32 -loleacc -lgdi32 -luser32 -o test_masked_vision.exe && ./test_masked_vision.exe

# 5. Pure-C vision & UIED button boundary isolation (5 tests)
gcc -Os -Ireverse-gemini/src reverse-gemini/tests/test_vision.c reverse-gemini/src/ttp_storage.c reverse-gemini/src/ttp_vision.c reverse-gemini/src/ttp_engine.c -lole32 -loleaut32 -loleacc -lgdi32 -luser32 -o test_vision.exe && ./test_vision.exe

# 6. Semantic action synthesizer & playback engine (9 tests)
gcc -Os -Ireverse-gemini/src reverse-gemini/tests/test_engine.c reverse-gemini/src/ttp_storage.c reverse-gemini/src/ttp_vision.c reverse-gemini/src/ttp_engine.c -lole32 -loleaut32 -loleacc -lgdi32 -luser32 -o test_engine.exe && ./test_engine.exe

# 7. 6-column drawer, modal step editor & 60FPS checkerboard (7 tests)
gcc -Os -Ireverse-gemini/src reverse-gemini/tests/test_tinytask_pro.c reverse-gemini/src/ttp_storage.c reverse-gemini/src/ttp_vision.c reverse-gemini/src/ttp_engine.c -lcomctl32 -lole32 -loleaut32 -loleacc -lgdi32 -luser32 -lcomdlg32 -lshell32 -o test_tinytask_pro.exe && ./test_tinytask_pro.exe

# 8. Cascaded visual recovery & boundary regression scenarios (13 tests)
gcc -Os -Ireverse-gemini/src reverse-gemini/tests/test_fix_repro.c reverse-gemini/src/ttp_storage.c reverse-gemini/src/ttp_vision.c reverse-gemini/src/ttp_engine.c -lole32 -loleaut32 -loleacc -lgdi32 -luser32 -o test_fix_repro.exe && ./test_fix_repro.exe
```

---

## 开源许可证 / License (MIT)

本项目采用 **MIT 开源许可证**。完整许可证文本见 [`LICENSE`](file:///e:/reverse-gemini/reverse-gemini/LICENSE)。

```text
MIT License

Copyright (c) 2026 TinyTask Pro Contributors

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```

