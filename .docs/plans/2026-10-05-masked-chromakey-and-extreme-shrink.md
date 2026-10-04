# TinyTask Pro: 自适应背景透明化掩码搜图与阶段二极限瘦身实施计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 实现录制时自动外边缘色差连通泛洪（Border Chromakey）生成 32bpp BGRA 透明掩码，构建原生 Win32 步骤微调弹窗与 60FPS 灰白棋盘格实时预览，深度融合阶段二零堆内存分层掩码搜图引擎（Masked Probe SAD + Masked NCC），彻底剥离 CRT 浮点库并将内嵌位图转为调色板位图，使二进制产物体积达到 **50 ~ 60 KB**，并在完成后更新项目 README 文档。

**Architecture:** 
1. 录制层通过外边界基准色彩采样与 4 邻域 BFS 连通泛洪生成 $A \in \{0, 255\}$ 的 32bpp BGRA 位图，保留老版本 24bpp 宏兼容；
2. UI 壳层通过纯 Win32 原生 TrackBar 和 GDI 双画刷棋盘格提供微秒级响应的实时透明容差微调；
3. 搜图层结合 BSS 静态全屏灰度缓冲区，仅在 $A=255$ 前景提取 16 个特征探测点进行 $<1\text{ms}$ 极速 SAD 过滤，并在 Tier 2 执行背景权重归零的 Masked NCC 定点整数精算；
4. 资源与代码瘦身：压缩 `toolbar.bmp` 为紧凑调色板位图并以牛顿开方 `isqrt` 斩断 `math.h` 依赖，达成 50~60 KB 终极体积门禁。

**Tech Stack:** C (C99/C11), Win32 API, GDI, `comctl32.dll` (TrackBar, ListView), MinGW-w64 GCC (`-Os -s -flto -ffunction-sections -fdata-sections -Wl,--gc-sections`).

## Primary Spec Reference
- `.docs/specs/2026-10-05-masked-chromakey-vision-and-extreme-shrink-design.md`

## Global Constraints
- AI 严禁在工作区根目录新建任何文件，所有代码、文档、配置必须写在二级目录 `reverse-gemini/` 内（根目录仅允许 `AGENTS.md`、`.gitignore`、`README.md`）。
- 绝不使用 UPX 等加壳工具，保持纯原生 PE 结构，确保 0 杀软报毒。
- 二进制体积终极门禁：编译出的 `bin/tinytask_pro.exe` 必须 $\le 61,440 \text{ 字节}$（$\le 60 \text{ KB}$）。
- 最终任务必须包含 README.md 文档更新。

---

## 冲击面扫描 (Impact Surface Scan)

| 制品类型 | ⬆️ 上游（生产方） | ⬇️ 下游（消费方） | 处理策略 |
|---|---|---|---|
| **数据/存储** | `ttp_adaptive_crop_button` 生成 32bpp BMP | `ttp_storage.c` 序列化/反序列化 | 自动识别 24bpp vs 32bpp，向后 100% 兼容旧宏 |
| **函数/接口** | `ttp_chromakey_mask` 生成单通道掩码 | `ttp_vision.c` 搜图引擎 | 独立单元测试覆盖边缘泛洪与防误抠回退 |
| **搜图算法** | `s_screenGray` 静态灰度与定点 Masked NCC | `ttp_engine.c` 回放注入 | 公共接口签名保持兼容，全真测试右上角移位 |
| **资源/文件** | `toolbar.bmp` 格式转换 | `tinytask_pro.rc` 工具栏载入 | 确保 8bpp 调色板位图在工具栏渲染色彩正常 |
| **编译依赖** | 剥离 `math.h` CRT 软浮点支持 | 链接器 `ld.exe` | 采用 `isqrt` 整数运算，消除浮点符号引用 |
| **文档** | 项目能力与体积变迁 | 用户与开发者阅读的 `README.md` | 最终任务专门负责规范更新 `README.md` |

---

### Task 1: 32bpp BGRA 资产存储与向下兼容协议

**Files:**
- Modify: `reverse-gemini/src/ttp_core.h:35-55`
- Modify: `reverse-gemini/src/ttp_storage.c:20-150`
- Test: `reverse-gemini/tests/test_storage.c:1-120`

**Interfaces:**
- Consumes: `TTPStep`, `BITMAPFILEHEADER`, `BITMAPINFOHEADER`
- Produces: `TTPStep.chromaTol`, 32bpp BMP 存储与 24bpp 自动补齐 $A=255$ 兼容加载

- [ ] **Step 1: 编写 32bpp 存储与 24bpp 兼容性失败测试**

在 `reverse-gemini/tests/test_storage.c` 中添加针对 32bpp BGRA 掩码存储以及 24bpp 旧宏无缝加载的断言测试：
```c
// Test 32bpp BMP saving & loading with alpha channel
TTPStep step32;
memset(&step32, 0, sizeof(step32));
step32.stepId = 1;
step32.chromaTol = 30;
// Verify 32bpp bitmap preserves alpha bytes
```

- [ ] **Step 2: 运行测试验证失败**

运行：
```powershell
gcc -Os -I reverse-gemini/src reverse-gemini/tests/test_storage.c reverse-gemini/src/ttp_storage.c -o reverse-gemini/tests/test_storage.exe
.\reverse-gemini\tests\test_storage.exe
```
预期：FAIL（编译报错 `chromaTol` 字段未定义）。

- [ ] **Step 3: 实现 `ttp_core.h` 字段扩展与 `ttp_storage.c` 32bpp 读写**

在 `ttp_core.h` 中为 `TTPStep` 增加 `BYTE chromaTol` 字段；在 `ttp_storage.c` 中完善对 `biBitCount == 32` 的打包保存与对 `biBitCount == 24` 的自动升阶补齐逻辑。

- [ ] **Step 4: 运行测试验证通过**

运行测试，验证 32bpp 与 24bpp 双向往返测试 100% PASS。

- [ ] **Step 5: 提交 Task 1**

```bash
git add reverse-gemini/src/ttp_core.h reverse-gemini/src/ttp_storage.c reverse-gemini/tests/test_storage.c
git commit -m "feat(storage): support 32bpp BGRA alpha mask assets with backward compatibility"
```

---

### Task 2: 自动边缘色差连通泛洪算法与防误抠安全回退

**Files:**
- Create: `reverse-gemini/tests/test_chromakey.c`
- Modify: `reverse-gemini/src/ttp_vision.h:40-60`
- Modify: `reverse-gemini/src/ttp_vision.c:150-320`

**Interfaces:**
- Consumes: RGB 位图指针、宽高、容差阈值
- Produces: `BOOL ttp_chromakey_mask(const BYTE* rgbPixels, int w, int h, BYTE tol, BYTE* outMask)` 与 `ttp_adaptive_crop_button` 产出 32bpp 位图

- [ ] **Step 1: 编写色差泛洪算法单元测试**

新建 `reverse-gemini/tests/test_chromakey.c`：
1. 测试单色背景中央带有黑色实心图标的位图，验证外边缘被 100% 抠除（$M=0$），中心被 100% 保留（$M=1$）；
2. 测试带轻微渐变/噪点背景，验证在 $\Delta E \le 25$ 容差下完整抠除；
3. 测试极端复杂全杂色图案，验证触发安全门禁，返回全 1 前景未被误破坏。

- [ ] **Step 2: 运行测试验证失败**

运行：
```powershell
gcc -Os -I reverse-gemini/src reverse-gemini/tests/test_chromakey.c reverse-gemini/src/ttp_vision.c -lole32 -loleaut32 -loleacc -lgdi32 -luser32 -o reverse-gemini/tests/test_chromakey.exe
```
预期：FAIL（符号 `ttp_chromakey_mask` 未定义）。

- [ ] **Step 3: 在 `ttp_vision.c` 实现 BFS 外边缘泛洪与安全回退**

实现基于队列的轻量级 4 邻域连通泛洪，将生成的前景掩码赋给截取的 Alpha 通道。

- [ ] **Step 4: 运行测试验证通过**

运行：
```powershell
.\reverse-gemini\tests\test_chromakey.exe
```
预期：PASS（3 项子测试全绿）。

- [ ] **Step 5: 提交 Task 2**

```bash
git add reverse-gemini/src/ttp_vision.h reverse-gemini/src/ttp_vision.c reverse-gemini/tests/test_chromakey.c
git commit -m "feat(vision): implement border chromakey BFS flood-fill with safety fallback"
```

---

### Task 3: 阶段二极致瘦身：调色板位图转换与彻底切断 CRT 浮点库

**Files:**
- Modify: `reverse-gemini/src/toolbar.bmp` (转换为 8bpp/4bpp 紧凑调色板位图)
- Modify: `reverse-gemini/src/ttp_vision.c:50-120` (加入定点整型开方 `isqrt`，移除全局 double)

**Interfaces:**
- Consumes: 工具栏位图、开方计算
- Produces: 紧凑 `toolbar.bmp`（从 12.8 KB 降至 ~3 KB），`unsigned long isqrt(unsigned long long n)`

- [ ] **Step 1: 编写整数定点与开方精度测试**

验证 `isqrt()` 针对大整数（最高至 $2^{31}$）与标准 `sqrt()` 误差不超过 $\pm 1$：
```c
assert(isqrt(0) == 0);
assert(isqrt(100) == 10);
assert(isqrt(2560000) == 1600);
```

- [ ] **Step 2: 紧凑化 `toolbar.bmp` 并植入 `isqrt`**

1. 将 `toolbar.bmp` 用紧凑调色板位图重写替换，使 `.rsrc` 段净减约 9 KB；
2. 在 `ttp_vision.c` 实现无需浮点库的快速整数位移牛顿开方算法 `isqrt`。

- [ ] **Step 3: 编译验证体积缩减与功能完整**

编译并检查静态体积变化：
```powershell
Get-Item reverse-gemini/src/toolbar.bmp | Select-Object Length
```
预期：体积从 12,870 降至 $\le 4,000$ 字节。

- [ ] **Step 4: 提交 Task 3**

```bash
git add reverse-gemini/src/toolbar.bmp reverse-gemini/src/ttp_vision.c
git commit -m "perf(shrink): compact toolbar bitmap and introduce integer isqrt routine"
```

---

### Task 4: 极速零堆内存与掩码分层搜图引擎 (Masked Cascaded Engine)

**Files:**
- Create: `reverse-gemini/tests/test_masked_vision.c`
- Modify: `reverse-gemini/src/ttp_vision.c:600-1150`
- Modify: `reverse-gemini/src/ttp_engine.c:580-720`

**Interfaces:**
- Consumes: 32bpp 模板位图（带 Alpha）、屏幕 DC
- Produces:
  - 静态单例 `s_screenGray`（零动态分配）
  - Tier 1: 掩码探测点 SAD 粗筛（`ttp_match_template_masked_probe_sad`）
  - Tier 2: 掩码归一化互相关（`ttp_match_template_masked_ncc`）

- [ ] **Step 1: 编写右上角极端壁纸变幻回归测试**

在 `reverse-gemini/tests/test_masked_vision.c` 中：
1. 载入用户的真实素材 `step01_CLICK_iKuuuVPN.bmp`；
2. 构建底色为左下角深色的录制图，并将其移位放置在 2560×1440 画布右上角的浅蓝高亮壁纸区；
3. 断言掩码匹配得分 $\ge 0.90$（原先为 0.6916），在标准 $0.70$ 阈值下瞬间定位。

- [ ] **Step 2: 运行测试验证失败**

运行测试，验证在当前无掩码版本下得分仅为 0.69 导致断言失败。

- [ ] **Step 3: 实现掩码探测 SAD 与 Masked NCC 定点搜图**

在 `ttp_vision.c` 中：
1. 引入静态 BSS `s_screenGray` 灰度缓冲；
2. 仅在前景提取 16 个高频特征探测点进行 $<1\text{ms}$ 的快速 SAD 跳步；
3. 实现背景权重置 0 的 Masked NCC 精确核函数；
4. `ttp_engine.c` 对接新的掩码搜图通道。

- [ ] **Step 4: 运行测试验证通过**

运行测试：
```powershell
gcc -Os -I reverse-gemini/src reverse-gemini/tests/test_masked_vision.c reverse-gemini/src/ttp_storage.c reverse-gemini/src/ttp_vision.c reverse-gemini/src/ttp_engine.c -lole32 -loleaut32 -loleacc -lgdi32 -luser32 -o reverse-gemini/tests/test_masked_vision.exe
.\reverse-gemini\tests\test_masked_vision.exe
```
预期：PASS（右上角变幻壁纸得分达 0.95+，耗时 $<20\text{ ms}$）。

- [ ] **Step 5: 提交 Task 4**

```bash
git add reverse-gemini/src/ttp_vision.c reverse-gemini/src/ttp_engine.c reverse-gemini/tests/test_masked_vision.c
git commit -m "feat(vision): integrate zero-heap masked probe SAD and masked NCC vision engine"
```

---

### Task 5: 原生 Win32 步骤微调弹窗与 60FPS 灰白棋盘格实时预览

**Files:**
- Modify: `reverse-gemini/src/tinytask_pro.rc:40-100`
- Modify: `reverse-gemini/src/tinytask_pro.c:300-600`

**Interfaces:**
- Consumes: 用户双击或右键步骤、`TTPStep` 图像资产
- Produces: 原生模态编辑对话框、0~100 TrackBar 消息处理、GDI 8x8 棋盘格实时 Alpha 混合重绘

- [ ] **Step 1: 编写对话框注册与消息映射单元测试**

在 `reverse-gemini/tests/test_tinytask_pro.c` 中验证步骤编辑对话框资源 ID 与类名注册有效性。

- [ ] **Step 2: 在 `tinytask_pro.rc` 与 `tinytask_pro.c` 实现编辑弹窗**

1. 定义轻量级对话框模板（预览 Static、TrackBar、Check box、确定/取消按钮）；
2. 监听 `WM_HSCROLL` 响应滑动事件，调用 Task 2 的 `ttp_chromakey_mask` 动态重算并在 Static 区域绘制灰白棋盘格；
3. 点击确定将更新后的位图和 `chromaTol` 写回项目工程。

- [ ] **Step 3: 编译并验证对话框渲染与响应**

编译生成 `tinytask_pro.exe`，启动点检双击步骤能否弹出平滑 60FPS 实时预览弹窗。

- [ ] **Step 4: 提交 Task 5**

```bash
git add reverse-gemini/src/tinytask_pro.rc reverse-gemini/src/tinytask_pro.c reverse-gemini/tests/test_tinytask_pro.c
git commit -m "feat(ui): add native step edit dialog with 60fps checkerboard real-time preview"
```

---

### Task 6: 终极 50~60 KB 体积门禁、全量回归验证与 README 文档更新

**Files:**
- Modify: `README.md` (或 `reverse-gemini/README.md`)
- Build: `reverse-gemini/bin/tinytask_pro.exe`
- Test: 全量单元测试套件 (`test_storage.exe`, `test_engine.exe`, `test_chromakey.exe`, `test_masked_vision.exe`, `test_fix_repro.exe`)

**Interfaces:**
- Consumes: 全量优化编译指令
- Produces: 最终二进制文件 $\le 60 \text{ KB}$，更新完毕的技术与产品自述文档

- [ ] **Step 1: 全量优化编译与体积门禁验收**

运行最终构建：
```powershell
windres reverse-gemini/src/tinytask_pro.rc -O coff -o reverse-gemini/src/tinytask_pro_res.o
gcc -Os -s -flto -ffunction-sections -fdata-sections "-Wl,--gc-sections" -fno-asynchronous-unwind-tables -fno-ident -mwindows reverse-gemini/src/tinytask_pro.c reverse-gemini/src/ttp_storage.c reverse-gemini/src/ttp_vision.c reverse-gemini/src/ttp_engine.c reverse-gemini/src/tinytask_pro_res.o -lcomctl32 -loleacc -lole32 -loleaut32 -lgdi32 -luser32 -lcomdlg32 -lshell32 -o reverse-gemini/bin/tinytask_pro.exe
Get-Item reverse-gemini/bin/tinytask_pro.exe | Select-Object Name, Length
```
硬门禁要求：`Length <= 61440`（严格 $\le 60 \text{ KB}$）。

- [ ] **Step 2: 运行全量测试套件回归点检**

运行：
```powershell
.\reverse-gemini\tests\test_storage.exe
.\reverse-gemini\tests\test_engine.exe
.\reverse-gemini\tests\test_chromakey.exe
.\reverse-gemini\tests\test_masked_vision.exe
.\reverse-gemini\tests\test_fix_repro.exe
```
预期：所有测试全部通过（100% 绿灯）。

- [ ] **Step 3: 更新项目 README 文档**

按用户明确要求，系统性更新项目根目录及子目录内的 `README.md`：
1. 增加 TinyTask Pro **自适应背景透明化掩码搜图（Masked Template Matching）**特性说明；
2. 介绍步骤编辑弹窗与灰白棋盘格实时预览功能；
3. 更新二进制体积达到 **~55 KB** 极致轻量化的技术里程碑；
4. 记录微秒级零堆内存搜图架构。

- [ ] **Step 4: 最终提交**

```bash
git add README.md reverse-gemini/README.md reverse-gemini/bin/tinytask_pro.exe
git commit -m "docs: update README with masked vision features and finalize 50-60KB release"
```
