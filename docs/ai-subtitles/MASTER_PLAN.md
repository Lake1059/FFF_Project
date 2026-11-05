# 3FP 实时 AI 字幕：完整实施规划（总纲）

> 日期：2026-10-22 ｜ 分支：`feature/realtime-ai-subtitles-qwen3-asr`（worktree `C:/PLAN/3FCompare/_3fp-ai-subtitles`，基线 `1a930b0`）
> 本文是**唯一总纲**：状态盘点、决策清单、路线图、分阶段门禁。各专题文档是附件，不重复其内容。

## 0. 文档地图

| 文档 | 内容 | 状态 |
|---|---|---|
| **MASTER_PLAN.md（本文）** | 总纲：状态、决策、路线图、门禁 | 本文 |
| `PROJECT_PLAN.md` | Stage 0 仓库隔离与架构调查、首版范围、Stage 1 门禁表 | ✅ 已完成 |
| `RUNTIME_EVALUATION.md` | 运行时实测（GPU/CPU、RTF、路线对比） | ✅ |
| `MODEL_PERFORMANCE_ANALYSIS.md` | 开销归因（launch-bound）、优化空间 | ✅ |
| `QUANTIZATION_ANALYSIS.md` | 量化档位全图、VRAM/速度/质量、CER 门禁 | ✅ |
| `IMPLEMENTATION_ANALYSIS.md` | 落码设计（挂点 file:line、ABI、worker 协议、线程边界、2a–2d） | ✅ |
| `CER_BASELINE_RESULTS.md` | P1.2/P1.3 的 CER 对撞原始数据 | ✅ |
| `P5_E2E_RESULTS.md` | 真机端到端：延迟账、显存、三个真机缺陷的根因 | ✅ |
| `PACK_FORMAT.md` | **扩展包契约**（本体与包之间唯一的接口） | ✅ |
| `USER_GUIDE.md` | 用户向：装包、模型、启用、已知限制 | ✅ |
| `BACKEND_MATRIX.md` | N/I/A/CPU 各自怎么跑、`-ngl` 与 `--device`、未验证项 | ✅ |

> 读法：想知道**功能怎么用**看 `USER_GUIDE.md`；想**做一个包**看 `PACK_FORMAT.md`；
> 想**换显卡/纯 CPU**看 `BACKEND_MATRIX.md`；想知道**当初为什么这么设计**看
> `IMPLEMENTATION_ANALYSIS.md` 与本文 §4 的决策表。

---

## 1. 目标与边界（一句话版）

在 3FP 实验分支上实现：**从音频解码链旁路 PCM → 独立 ASR 进程识别 → 以媒体时间轴注入现有字幕图层**。首版只做必要能力（见 `PROJECT_PLAN.md` §1 排除项清单），ASR 任何失败只关字幕不碰播放，零 ABI 改动、零新 D3D 层。

---

## 2. 当前状态总盘点

| # | 工作项 | 状态 | 证据/位置 |
|---|---|---|---|
| S0-1 | Git 隔离（worktree，不碰脏工作树） | ✅ | `PROJECT_PLAN.md` §2 |
| S0-2 | 架构调查（调用链/时钟/字幕/ABI 纪律，12 条 file:line 事实） | ✅ | `IMPLEMENTATION_ANALYSIS.md` §3 |
| S1-1 | 模型获取与校验（HF safetensors + GGUF，SHA-256） | ✅ | `RUNTIME_EVALUATION.md` §1 |
| S1-2 | GPU bf16 基准（加载 5.43s、RTF 0.23–0.50、VRAM/RSS） | ✅ | `results/transformers_gpu_zh.json` |
| S1-3 | 英文短样本识别 | ✅ | `RUNTIME_EVALUATION.md` |
| S1-4 | 15 分钟连续运行 | ✅ | llama.cpp Q8_0 181/181 窗零错误、RSS/VRAM 平（`P5_E2E_RESULTS.md` §3） |
| S1-5 | llama.cpp 路线实测 | ✅ | Q8_0 CUDA：rtf 0.024–0.077 |
| S1-6 | CPU 降级基准 | ✅ | `-ngl 0 --device none`：rtf 0.138–0.373，文本与 GPU 臂逐窗一致 |
| S2-1 | 落码设计（tap/导出/worker/状态机/线程边界） | ✅ | `IMPLEMENTATION_ANALYSIS.md` |
| S2-2 | 性能与量化专题 | ✅ | 两份分析文档 |
| S2-3 | **落码** | ✅ | 见下表 P2–P6 |

**阶段执行结果（2026-10-22 ~ 23，全部已提交）**

| 阶段 | 内容 | 状态 | 提交 | 关键证据 |
|---|---|---|---|---|
| P2a | 内核 tap（`AsrAudioTap` + `FFF.Player.AudioTap.h` 4 导出） | ✅ | `cbc0f6a` | 关闭零成本；开启 dump 块声道/位置/代次正确，seek 后 ring 即空 |
| P3 | worker + pump/result 线程 + 协议 | ✅ | `cbc0f6a` | 手工喂帧可复现；Kill worker 播放器存活 |
| P4 | 显示注入 + 状态机 + 设置页 + 菜单 | ✅ | `6b310b2` | 外部 SRT 有无两态不重叠 |
| P5 | 真机端到端（42.6 s 中英混合） | ✅ | `3880d09` | 8/8 窗命中上屏；滞后 ≈ 一次推理（0.3–0.75 s） |
| P6 | 包化（本体与 AI 解耦）+ 交付文档 | ✅ | `c8ab6d8` / `5d8cc05` / `26a34dd` | 选到有效包即启用；无包/坏包静默停用 |
| P6+ | 纯音频（无视频轨）也能显示 | ✅ | `5d8cc05` | 内核 `subtitleLayoutEnabled_` latch + 宿主 `无视频时也绘制`；`呈现帧数 3→4` |
| P6+ | 推理后端可选（CPU/CUDA/Vulkan/ROCm/SYCL） | ✅ | `26a34dd` | `servers` 映射 + `--list-devices` 探测；CPU 臂 rtf 0.138–0.303 |
| P6+ | 字幕语言可指定（30 种）+ 中英混说档 + 闸门 | ✅ | `106cb62` | 语言名走英文名（ISO 无效）；混说不传提示、闸门放行中英标签 |
| P6+ | 模型档位（标准/低配）与 Q4_K_M 回退 | ✅ | 本轮 | `models` 映射 + 高级"模型档位"；Q4_K_M 456 MiB 本机量化产出 |
| P6+ | 派发环境临时目录（`--tmp-dir` + 回退链） | ✅ | 本轮 | schtasks 派发下系统临时目录整个不可用（Python `FileNotFoundError`，worker 启动即死）→ 宿主传可写根（包目录/程序目录下 `tmp`，探针验证）+ 子进程 `TEMP/TMP`；worker 四级回退；见 `IMPLEMENTATION_ANALYSIS.md` §6 |
| P6+ | 设置页高级项默认展开 + 出盒状态验收 + worker 包分发 | ✅ | 本轮 | 窗口/步长/设备序号/模型档直接可调；出盒（cpu/auto/默认档/闸门关）11 窗上屏 0 异常；`3FP-AI-Subtitles-Pack-20261023.zip`（E:\WorkBuddy\3FP-AI-Subtitles\） |
| P6+ | 字幕语言面板只列 4 主项（未验证语种不出现） | ✅ | 本轮 | 用户裁定"未验证的内容不出现"：下拉只列自动/中文/English/中英混说；`规整` 收紧到主项（残留 `ja` 之类 → auto，杜绝"显示自动、锁着日语"）；整表留 `全部` 仅供防漂移；`check_l0` L0-1b + 单测 10 条看守 |
| P6+ | 默认设备改自动（GPU）+ GPU 层数可调 | ✅ | 本轮 | 用户裁定"默认自动选 GPU"：持久化 cpu→auto（类默认本就是 auto 语义）；高级区加「GPU 层数」（`--n-gpu-layers`，留空=全卸 28 层，显存吃紧填小数，CPU 后端置灰）；全链路 `设置.vb→控制器(重启判据)→管道 --ngl→worker 覆盖`；llama-server 参数盘点见 `MODEL_TIERS.md` §7 |

> 门禁未闭合项只剩 B1（seek/暂停/切文件的清屏矩阵实测）与 B2（非 CUDA 后端的
> mmproj 完整性）——两者都不是代码缺口，见 `BACKEND_MATRIX.md` §7 与
> `USER_GUIDE.md` §5。

---

## 3. 路线图总览

```
P0 决策确认 ──┬─→ P1 Stage 1 收尾（路线实测，需用户配合 VRAM 窗口）─┐
（本周，纯确认）│                                                    ├─→ P2 2a 内核取数 ─→ P3 2b 搬运识别 ─→ P4 2c 显示注入 ─→ P5 2d 实机验收 ─→ P6 交付
              └─→ P2a 可立即启动（不依赖 ASR 路线，见 §5 并行分析）──┘
```

✅ **上图已全部走完**（2026-10-22 ~ 23）。后续新增的两块能力——纯音频字幕路径、
推理后端可选——都是在 P6 之后按需求追加的，不改动本图的阶段划分：前者是宿主多一个
开关 + 内核一个 latch，后者是包清单多两个字段 + worker 启动时探测设备。

| 阶段 | 内容 | 工作量 | 通过门禁（退出条件） |
|---|---|---|---|
| **P0** | 决策点 D1–D9 拍板 | S（用户时间） | 全部决策有结论并回写文档 |
| **P1** | Stage 1 收尾：llama.cpp Q8_0 冒烟、CER 基线、15 分钟连续、VRAM 实测 | M（+用户配合） | 首选路线三样本识别正确、RTF/VRAM/积压落盘、15 分钟无崩溃 |
| **P2（2a）** | 内核 tap + 导出 + session/renderer 挂载 | M | 关闭零成本；开启 dump 块验证（声道/位置/代次）；seek 后 ring 即空 |
| **P3（2b）** | worker + pump/result 线程 + 协议 | M | 手工喂帧可复现；15 分钟无泄漏；worker 被杀播放无感 |
| **P4（2c）** | 事件注入 + 状态机 + 设置 + 菜单 | M | 外部 SRT 有无两态不重叠；seek/暂停/停止/切文件无僵死字幕；推理崩只关字幕 |
| **P5（2d）** | 真机端到端 + 延迟账 + 资源占用 | S | 中英素材端到端正确；延迟/显存/CPU 记录 |
| **P6** | README（venv 搭建+启用步骤）、提交整理、交接文档 | S | 分支可被他人按文档复现 |

每阶段独立提交；任一阶段回归 → 回退该提交，不碰 main。

---

## 4. 分阶段详规

### P0 决策确认 ✅ 已拍板（2026-10-22，用户放行"完整开始推进"，采用建议值）

| # | 决策 | 选项 | **结论** | 影响面 |
|---|---|---|---|---|
| D1 | 窗口参数 | 5s/5s vs 3s/3s | **5s/5s**（RTF 余量大；3s 精度与余量双降） | worker 两行参数 |
| D2 | Python 运行时 | venv 搭建文档 vs 内置运行时目录 | **venv 文档**（+2.5GB 内置无必要） | README |
| D3 | tap 外部可达 | pull 导出 vs 回调表 | **pull**（`IMPLEMENTATION_ANALYSIS.md` §4.4） | 已设计 |
| D4 | 显示层级 | AI 与外部 SRT 同底并存（AI 上移）vs AI 开启时暂停外部字幕 | **同底并存 + AI 上移一行** | P4 一个判断 |
| D5 | 语言 | 自动检测 vs 固定中文 | **自动检测**（实测可用；worker 侧一行可改固定） | worker 参数 |
| D6 | 显示策略 | latest-wins + staleness guard | **latest-wins + staleness guard** | 已设计 |
| **D7** | **推理路线** | llama.cpp（Q8_0/Q6_K 自量化）vs transformers + torch.compile | **llama.cpp 优先**（VRAM 0.95→0.85G + decode 2–4×，解 WDDM 崩溃）；mmproj CER 不合格则兜底 transformers+compile | worker 内部实现（**宿主侧协议不变**） |
| **D8** | **量化档位** | Q8_0 基线 → Q6_K 日常 → Q4_K_M 应急 | **三层架构**（`QUANTIZATION_ANALYSIS.md` §5） | 模型目录内容 |
| **D9** | **worker 形态**（随 D7） | (a) Python 内嵌 llama.cpp 绑定 (b) worker 托管常驻 `llama-server`，自身做音频调理+HTTP 推理 | **(b)**：pip 依赖最轻；server 崩了 worker 可重启它；宿主仍只见一个子进程 | worker 架构 |

> **路线不影响接口的关键设计**：worker 的 stdin/stdout 协议（PCM 帧进、JSON 行出）在 D7 两路线下完全相同——这是 `PROJECT_PLAN.md` §4.1 统一接口的兑现。D7 只换 worker 的"推理那一格"。

### P1 Stage 1 收尾（需用户配合：关桌面应用，保证 VRAM 余量 ≥2G）

前置：复制 `torch/lib/{cudart64_13,cublas64_13,cublasLt64_13}.dll` 到 `llama-b11669/cuda/`（免 404MB 下载，已核实存在）。

| 步骤 | 动作 | 产出 | 判据 |
|---|---|---|---|
| 1.1 | Q8_0 CUDA 冒烟（`llama-server -hf ...:Q8_0` 或 `-m + --mmproj`） | 冒烟日志 | 加载成功；确认跑在独显（4060）非集显；两样本转写正确 |
| 1.2 | CER 基线（Q8_0 vs bf16，四类样本集） | CER 对撞表 | ΔCER ≤0.5% abs；语言标签正确、无复读 |
| 1.3 | Q6_K 自量化（`llama-quantize`，CPU 分钟级）+ 复测 1.2 | Q6_K GGUF + CER | ΔCER(Q6_K vs Q8_0) ≤0.3% abs |
| 1.4 | 15 分钟连续（5s 窗/5s 步，VRAM≥2G 时） | 连续运行 JSON | 无崩溃；RSS/VRAM 平稳；积压行为记录 |
| 1.5 | 稳态 VRAM/CPU 实测 | nvidia-smi 采样 | 与估算对撞，修正 `QUANTIZATION_ANALYSIS.md` §3 |

产出回写 `RUNTIME_EVALUATION.md`；若 Q8_0 音频路径 CER 不合格 → 触发 D7 兜底（transformers+compile），P1 改为该路线重跑。

### P2（2a）内核取数 — 可与 P1 并行

按 `IMPLEMENTATION_ANALYSIS.md` §4：`AsrAudioTap.{h,cpp}` 新文件 + `WasapiRenderer` 两行挂载 + `PlayerSession` 成员/seek 盖章 + `FFF.Player.AudioTap.h` 导出 + vcxproj 登记。

- 验证手段：开启 tap 后 dump 块到文件（声道数/采样率/position 连续/代次盖戳/seek 后 ring 即空）；A/B 关闭时 `Enqueue` 成本（同码率无差异）。
- **不依赖 ASR 路线**：dump 的是原始帧，与后端选择无关。
- 构建：`dotnet build` 直跑；内核用项目既有脚本（`_3fc_tmp/build_kernel.py Release <绝对路径 vcxproj>`）。

### P3（2b）搬运与识别

- worker（D9 形态）：stdin/stdout 协议按 `IMPLEMENTATION_ANALYSIS.md` §6；16k 下混/分窗/推理按 D1/D5 参数；llama-server 托管 + 健康检查 + 失败重启一次。
- 宿主：`Ai字幕管道.vb`（pump/result 线程）+ `Ai字幕状态机.vb`。
- 验证：手工向 worker stdin 喂 P2 录制的帧复现识别；worker 被 Kill → 播放无感、字幕关闭。

### P4（2c）显示注入

- 事件参数加 `时间轴代次` 属性；`Form1.vb` 订阅 `绘制扩展定时文字`；状态机 latest-wins + staleness guard；设置页 4 项 + 菜单复选项（照歌词范式）。
- 验证矩阵：外部 SRT 有/无 × 播放/seek/暂停/停止/切文件，无僵死字幕、无重叠、无闪烁；AI 推理崩溃（Kill worker）只关字幕。

### P5（2d）真机验收

- ⚠️ **沙箱约束**：播放器走 DXGI 路径，Bash/PowerShell 子进程必崩（tsbx 注入）⇒ 真机运行用 **schtasks 派发**、日志落盘再读（项目既有实践）。
- 端到端：中/英素材各一段（`testmedia/media/test.mp4` 音轨）；延迟账实测（预期 0.8–2.7s，路线依赖）；CPU/VRAM 占用记录。
- 通过后进入 P6。

### P6 交付收尾

- README：venv 搭建（`pip install torch --index-url .../cu132 transformers soundfile` + llama.cpp 说明）、模型放置、启用步骤、已知限制（1.2–2.7s 滞后等）。
- 提交整理：每阶段独立提交的 squash/信息规范化；分支 diff 自查（只含预期文件）。
- 交接文档：复现步骤 + 决策记录回指本文。

---

## 5. 并行与依赖分析

- **P2a 与 P1 可并行**：tap 的验证（dump 块）不需要 ASR 路线；P1 的 GPU 时间窗受用户配合限制时，先把内核侧做完是纯赚。
- **P3 依赖 P1 结论（D7/D9）**：worker 内部实现随路线变；但协议已冻结，宿主侧 pump/result 线程代码两条路线下相同——可先写宿主侧，worker 推理格后接。
- **P4 依赖 P3 的状态机接口**（`取当前条目`），可先行写事件参数属性与 Form1 订阅（不依赖 worker）。
- 建议执行序：**P0 决策 →（P1 ‖ P2a）→ P3 → P4 → P5 → P6**。

---

## 6. 资源与前置条件清单

| 项 | 状态 |
|---|---|
| 模型文件（safetensors / GGUF Q8_0 双份） | ✅ 就位（E 盘） |
| llama.cpp b11669 CUDA 二进制 | ✅ 解压 |
| CUDA 运行时 DLL | ✅ torch/lib 复制即可（免下载） |
| Q6_K 自量化 | ✅ 本地 llama-quantize（P1 步骤 1.3 执行） |
| torch venv（cu132 + transformers 5.20.2） | ✅ 就位（`envs/default/Scripts/python.exe`） |
| 磁盘 | ⚠️ E 盘为主（C 盘曾被 torch 占满，剩余 ~0.4G 的历史）；新文件落 E 盘 |
| GPU 时间窗 | ⏳ **需用户配合**：P1/P5 要求关浏览器/QQ，VRAM 余量 ≥2G |
| 测试素材 | ⚠️ BGM/歌曲类样本待补（CER 门禁第四类） |

---

## 7. 风险登记（合并版）

| 风险 | 等级 | 处置 |
|---|---|---|
| 15 分钟连续再崩（若 VRAM 窗口没配合好） | 中 | P1 前置条件写明 ≥2G 余量；Q8_0/Q6_K 总量 ~1.2–1.4G，比 bf16 1.85G 安全边际大 |
| llama.cpp mmproj 音频路径 CER 不合格 | 中 | D7 兜底：transformers + torch.compile（官方 2.4×）重跑 P1 |
| 小模型量化掉点（Q6_K/Q4） | 中 | CER 门禁（Δ ≤0.3%/1.0%），超阈回退档位 |
| Python 依赖重（torch 2.5G） | 中 | venv 文档；检测不到运行时 → 菜单置灰 + 信息图层提示 |
| 固有滞后 1.2–2.7s | **物理下限** | latest-wins + staleness guard；文档明示 |
| 沙箱 DXGI 崩溃影响真机测试 | 已定位 | schtasks 派发、日志落盘 |
| 多字幕源视觉重叠 | 低 | AI 上移一行（D4） |
| 全声道平均下混改变对白语义 | 低 | v1 注明；块头带 `positions[]`，v2 改 C+L+R |

---

## 8. 里程碑总表

| 里程碑 | 标志 | 依赖 |
|---|---|---|
| M1 设计冻结 | P0 九项决策落定 | 用户拍板 |
| M2 运行时验证通过 | P1 全步骤判据达成 | 用户 GPU 时间窗 |
| M3 取数可用 | P2a 门禁通过 | M1（可与 M2 并行） |
| M4 识别可用 | P3 门禁通过 | M2 + M3 |
| M5 显示可用 | P4 门禁通过 | M4 |
| M6 实验版可交付 | P5 验收 + P6 文档 | M5 |
