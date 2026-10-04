# TinyTask Pro: 二进制体积极限精简与微秒级分层搜图引擎设计文档

**文档编号:** SPEC-20261005-01  
**状态:** 已批准 (Approved)  
**日期:** 2026-10-05  
**目标代码目录:** `reverse-gemini/`  

---

## 1. 背景与目标 (Context & Objectives)

TinyTask Pro 是一款面向 Windows 桌面自动化的超轻量级工具。当前版本中：
- 编译生成的可执行文件 `bin/tinytask_pro.exe` 体积为 **99,840 字节 (~97.5 KB)**，对比经典原始版 `tinytask.exe` 的 **35,328 字节 (~34.5 KB)** 存在显著的体积优化空间。
- 当前搜图引擎 (`ttp_vision.c`) 单次全屏模板匹配需动态申请和释放超过 **45.6 MB** 的堆内存（包含 1080P 屏幕的 double 灰度矩阵及双全屏积分图），且基于纯标量 double 遍历，单次匹配耗时在 25 ~ 60 ms，存在较大的内存抖动与计算开销。

### 核心目标与量化门禁
1. **体积极致缩减 (Binary Size Budget)**：
   - 目标体积：从当前的 99.8 KB 缩减至 **$\le 60,000$ 字节 ($\le 60\text{ KB}$)**，体积降幅达 40% 以上。
   - 安全要求：不采用加壳工具（如 UPX），保持纯原生 PE 结构，确保 0 杀软误报。
2. **微秒级搜图与零堆内存 (High-Performance Zero-Heap Vision)**：
   - 搜图耗时：1080P 全屏单次搜图平均耗时从 25~60 ms 骤降至 **1.2 ~ 2.5 ms**（提速 20 倍以上）。
   - 动态堆分配：单次搜图全流程堆分配降为 **0 字节**，完全杜绝内存碎片与换页抖动。
3. **接口零破坏兼容 (Zero Breaking Changes)**：
   - 维持 `ttp_vision.h` 中公共接口签名与语义完全不变，上层调用者（`ttp_engine.c`、`tinytask_pro.c`）零改动平滑无缝接入。

---

## 2. 二进制体积压缩工程方案 (Binary Size Shrinking)

剖析当前 99,840 字节的 PE 段分布：
- `.text` 代码段：52.2 KB (53%)
- `.rsrc` 资源段：21.5 KB (22%)
- `.rdata` 只读数据：9.8 KB (10%)
- `.idata` 导入表：8.0 KB (8%)
- `.pdata` / `.xdata` 异常表：3.3 KB (3%)
- 其他段：2.5 KB

### 2.1 编译与链接参数优化
通过 GCC/MinGW-w64 进阶优化指令重构构建管线：
```bash
gcc -Os -flto -ffunction-sections -fdata-sections -Wl,--gc-sections \
    -fno-asynchronous-unwind-tables -fno-ident -mwindows \
    reverse-gemini/src/tinytask_pro.c \
    reverse-gemini/src/ttp_storage.c \
    reverse-gemini/src/ttp_vision.c \
    reverse-gemini/src/ttp_engine.c \
    reverse-gemini/src/tinytask_pro_res.o \
    -lcomctl32 -loleacc -lgdi32 -luser32 -lcomdlg32 -lshell32 \
    -o reverse-gemini/bin/tinytask_pro.exe
```

1. **`-Wl,--gc-sections`**：结合 `-ffunction-sections -fdata-sections`，在链接阶段自动剔除未调用的死代码、孤立数据结构及无效的 API 导入存根。
2. **`-flto` (Link-Time Optimization)**：开启全程序链接期内联与常量折叠，打破编译单元边界，消除未导出的冗余符号。
3. **`-fno-asynchronous-unwind-tables -fno-ident`**：剥离 MinGW 默认注入的 SEH 栈展开表（消除 `.pdata` 与 `.xdata` 段，直接净减 ~3.3 KB）及 GCC 版本标识字符串。

### 2.2 剥离 CRT 浮点与数学库依赖
- 当前代码大量引用 `double` 运算与 `sqrt()`，导致可执行文件隐式引入了 MinGW CRT 的多倍精度浮点仿真例程与数学支持库。
- 将搜图算法与空间几何计算全量整数化/定点化（采用 32-bit / 64-bit 整数与快速位移牛顿开方 `isqrt()`），消除对 `math.h` 复杂浮点库的依赖，使 `.text` 代码段缩减约 8 KB。

### 2.3 内嵌资源极致轻量化 (`.rsrc`)
- **工具栏位图 (`toolbar.bmp`)**：
  - 现存 12,870 字节位图为 24-bit 未压缩 RGB。
  - 转换为带调色板的 8-bit 或 4-bit 紧凑位图（或程序化无损调色板），体积可压减至 3~4 KB。
- **程序图标 (`tinytask.ico`)**：
  - 精简多层分辨率冗余，仅保留自动化所需的 16x16 与 32x32 基础尺寸层，体积从 7.4 KB 压减至 2~3 KB。

---

## 3. 分层级联搜图引擎设计 (Cascaded Vision Engine)

### 3.1 零堆内存管理与极速定点灰度化 (Zero-Heap Architecture)
1. **单例复用静态灰度缓冲区**：
   - 内部维护静态复用单通道 8-bit 屏幕灰度缓冲 `static BYTE s_screenGray[MAX_PIXELS]`（按需支持至 4K 分辨率 3840×2160，内存约 8.3 MB；主流 1080P 仅需 2.1 MB）。
   - 全程搜索无任何 `malloc()` 或 `calloc()`，单次找图堆内存分配严格为 **0 字节**。
2. **纯整型定点灰度化转换**：
   - 捕获屏幕 32bpp DIB 后，通过整型移位快速计算亮度：
     $$\text{Gray} = (R \times 77 + G \times 150 + B \times 29) \gg 8$$
   - 1080P 全屏灰度化耗时小于 0.5 ms，数据可完整融入现代 CPU 的 L2/L3 缓存。

### 3.2 第一级（Tier 1）：稀疏探测跳步 + 快速 SAD 早停 (Fast Probe Skip)
面向 95% 以上常规 UI 自动化场景，全屏搜索耗时仅 **1.2 ~ 2.5 ms**。

1. **自动提取 8 个特征探测点 (Probe Points Selection)**：
   - 对传入的模板图像（宽 $W$，高 $H$）提取 8 个高辨识度关键像素点 $P_0 \dots P_7$：
     - 4 个内角点：$(2, 2)$、$(W-3, 2)$、$(2, H-3)$、$(W-3, H-3)$（内缩 2 像素规避边缘反走样/抗锯齿）；
     - 1 个中心点：$(W/2, H/2)$；
     - 3 个局部梯度变化最大（色差对比最明显）的内部特征点。
   - 预计算各探测点的相对偏移 $(dx_k, dy_k)$ 及期望灰度值 $G_k$。
2. **全屏 0.001 微秒短路跳步 (Probe Skip Filter)**：
   - 在屏幕全图滑动比对时，优先仅比对这 8 个探测点：
     ```c
     if (abs(screen[y + dy0][x + dx0] - G0) > TOLERANCE) continue;
     if (abs(screen[y + dy1][x + dx1] - G1) > TOLERANCE) continue;
     ...
     ```
   - 默认灰度容差 $\text{TOLERANCE} = 18$。全屏 99.5% 以上的不相关背景在 1~3 次单周期整数减法后即被直接短路淘汰。
3. **极少数候选区 SAD 累加与动态早停 (SAD with Early Exit)**：
   - 仅对通过 8 点初筛的极少数候选区域计算绝对差值和（SAD）：
     $$\text{SAD} = \sum_{v=0}^{H-1} \sum_{u=0}^{W-1} |I(x+u, y+v) - T(u, v)|$$
   - 循环中维护 `runningSAD`，一旦累加值超过当前最佳阈值或已知最优匹配，立即 `break` 中断跳出。
4. **高确信度直接命中**：
   - 若最佳候选区域的单像素平均差值 $\overline{\Delta} \le 8$，判定为确定性匹配，立即输出目标中心点坐标并成功返回。

### 3.3 第二级（Tier 2）：定点数整型 NCC 兜底验证 (Fixed-Point NCC Fallback)
面向 5% 极端场景（大面积对比度失真、半透明磨砂遮罩或弱特征主题）。

1. **触发时机**：
   - 若 Tier 1 未能在全屏发现高置信度（$\overline{\Delta} \le 8$）的绝对匹配，但存在若干疑似候选点时触发。
2. **定点化整型 NCC 计算**：
   - 使用 64-bit 定点整数运算（Q15 格式，定点标度 32768）：
     $$\text{Score}_{Q15} = \frac{\sum (I - \bar{I})(T - \bar{T}) \ll 15}{\operatorname{isqrt}\left(\sum (I - \bar{I})^2\right) \cdot \operatorname{isqrt}\left(\sum (T - \bar{T})^2\right)}$$
   - 配合纯整数快速开方 `isqrt()`（基于 32-bit 位移牛顿迭代法，耗时仅几十纳秒）。
3. **局部 ROI 精细打分**：
   - Tier 2 仅对 Tier 1 筛选出的 Top-3 候选点周边小邻域（$\pm 16$ 像素）进行校验，避免全屏卷积计算，确保兜底耗时严格控制在 5 ~ 8 ms 内。

---

## 4. 接口契约与向后兼容性 (API Contracts)

对外保留在 `reverse-gemini/src/ttp_vision.h` 中的所有公共函数签名，保持 100% 兼容：
```c
/* 核心模板匹配接口（上层透明调用） */
BOOL ttp_match_template_ncc(HDC hdcScreen, int screenW, int screenH, 
                           const BYTE* bmpPattern, DWORD bmpSize, 
                           double minScore, POINT* outMatchPos, double* outScore);

BOOL ttp_match_template_ncc_roi(HDC hdcScreen, int roiX, int roiY, int roiRadius, 
                               const BYTE* bmpPattern, DWORD bmpSize, 
                               double minScore, POINT* outMatchPos, double* outScore);

/* 自适应按钮边缘裁剪（基于 Sobel + 结构闭运算） */
BOOL ttp_adaptive_crop_button(HDC hdcSrc, LONG clickX, LONG clickY, 
                             RECT* outRect, BYTE** outBmp, DWORD* outBmpSize);

/* 辅助函数 */
double ttp_calc_euclidean_dist(LONG x1, LONG y1, LONG x2, LONG y2);
int    ttp_pick_nearest_candidate(LONG origX, LONG origY, const POINT* candidates, int count);
BOOL   ttp_crop_rect_bmp(HDC hdcSrc, const RECT* cropRect, BYTE** outBmp, DWORD* outBmpSize);
void   ttp_free_bmp_buffer(BYTE* bmpBuffer);
```

**置信度归一化说明**：
- 内部在 Tier 1 命中时，根据平均单像素差值 $\overline{\Delta}$ 线性映射为标准等效置信度：
  $$\text{score} = 1.0 - \frac{\overline{\Delta}}{64.0}$$
- Tier 2 兜底命中时，将 Q15 定点分数映射为浮点：$\text{score} = \frac{\text{Score}_{Q15}}{32768.0}$。
- 对外输出的 `outScore` 维持在标准的 $0.0 \sim 1.0$ 之间，上层调用者完全无需感知底层是快速 SAD 还是 NCC。

---

## 5. 异常防御与容错策略 (Error Handling & Edge Cases)

1. **低方差/平坦模板保护**：
   - 提取探测点前计算模板方差，若方差 $\le 1.0$（纯白/纯黑/单色块），自动退化为全模板网格稀疏采样，防止探测点失效。
2. **越界与分辨率兼容保护**：
   - 严格检查屏幕宽/高与模板尺寸，若屏幕尺寸小于模板尺寸直接安全返回 `FALSE`。
   - 针对多显示器负坐标或跨屏边界，自动对 ROI 实施坐标钳位截断（Clamping）。
3. **GDI 资源绝对回收**：
   - 无论匹配成功与否，所有通过 `CreateCompatibleDC`、`CreateDIBSection` 申请的句柄均保证在退出前显式销毁，严防 GDI 句柄泄漏。

---

## 6. 测试与验证计划 (Verification Plan)

| 阶段 | 验证目标 | 验证命令/方法 | 验收门禁 (Hard Gates) |
|---|---|---|---|
| **Gate 1: 功能与精度** | 像素级定位准确性、抗噪性（$\pm 15$ 灰度偏差） | `gcc -Ireverse-gemini/src reverse-gemini/src/ttp_vision.c reverse-gemini/tests/test_vision.c -lgdi32 -luser32 -loleacc -o reverse-gemini/tests/test_vision.exe && ./reverse-gemini/tests/test_vision.exe` | 100% 测试项通过，像素定位误差 $\le 1$ 像素 |
| **Gate 2: 性能与内存** | 搜图耗时与堆内存动态分配 | 运行基准脚本测试 100 次全屏匹配 | 平均耗时 $\le 3.0\text{ ms}$；单次搜索堆内存动态分配量严格为 **0 字节** |
| **Gate 3: 全套回归** | 引擎与存储全链路集成 | 运行 `test_storage.exe`、`test_engine.exe`、`test_tinytask_pro.exe` | 现有所有测试用例 100% PASS |
| **Gate 4: 二进制体积** | 可执行文件大小优化 | `Get-Item reverse-gemini/bin/tinytask_pro.exe \| Select-Object Length` | 二进制体积 $\le 60,000$ 字节 ($\le 60\text{ KB}$) |
