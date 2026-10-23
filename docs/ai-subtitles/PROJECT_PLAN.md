# 3FP 实时 AI 字幕实验项目规划

> 实验分支：`feature/realtime-ai-subtitles-qwen3-asr`
> 工作树：`C:/PLAN/3FCompare/_3fp-ai-subtitles`
> 基线提交：`1a930b08ecc3d18de2bd82dc124400ee6a008023`
> **总纲：`MASTER_PLAN.md`**（状态盘点/决策清单/路线图/门禁）；本文聚焦 Stage 0–1
> 配套文档：`RUNTIME_EVALUATION.md`、`MODEL_PERFORMANCE_ANALYSIS.md`（性能开销归因与优化空间）、`QUANTIZATION_ANALYSIS.md`（量化选择）

## 1. 首版范围

包含：

- Windows x64、本地离线推理；优先 RTX 4060 8GB，并实验 CPU 降级。
- 从当前音频解码链取 PCM，带媒体 100ns 时间轴。
- seek、换轨、暂停、停止、文件切换生命周期管理。
- 临时字幕（可修正）与确认字幕（稳定条目）。
- 复用现有定时字幕图层显示。
- ASR 初始化或推理失败不影响播放。

不包含：

- 实时翻译、说话人分离、训练/微调、复杂上下文纠错。
- Qwen3-ForcedAligner 常驻与词级时间戳。
- 无视频且无封面的纯音频媒体（现有字幕图层在视频宽高为 0 时直接返回）。
- 播放倍速（当前代码没有倍速 API 或 UI，不能默认存在）。
- 同时正式集成多个 ASR 后端。

## 2. 阶段 0：仓库隔离

### 2.1 真实仓库边界

`C:/PLAN/3FCompare/3fp` 不是独立 Git 仓库，Git 会向上解析到 3FCompare 主仓；真正的 3FP/FFF_Project 仓库是 `C:/PLAN/3FCompare/3fp/kernel`。

原工作树状态（未被改动）：

- 分支 `main`，提交 `1a930b0`。
- 相对 `upstream/main` 为 ahead 28 / behind 24。
- 原工作树已有 17 个已跟踪修改和 3 个未跟踪测试文件。

隔离做法：从已提交 `HEAD` 创建独立 Git worktree，而不是把脏工作树切到新分支。原工作树继续保留全部未提交内容；实验分支不自动包含它们。若后续确需其中某项能力，再逐项评估并显式移植。

### 2.2 项目技术栈

| 层 | 真实实现 |
|---|---|
| 托管播放器 | VB.NET，WinForms + WPF，`net10.0-windows10.0.26100.0`，x64（`FFF.Player/FFF.Player.vbproj:5-26`） |
| 原生内核 | C++20、MSVC v145、x64 DLL（`FFF.Native/FFF.Native.vcxproj:24-39,63-112`） |
| 解码 | FFmpeg shared libraries，当前链接 avcodec 63 / avformat 63 / avutil 61 / swresample 7 |
| 音频输出 | 自有 WASAPI renderer |
| 视频/字幕呈现 | 自有 D3D11 + Direct2D/DirectWrite 定时文字管线 |
| 线程 | PlayerSession 工作线程、WASAPI 事件线程、软件视频解码线程、独立定时文字呈现线程、`LakeUI.PrecisionTimer` worker |
| 构建 | `3F_Project.slnx`；原生 MSBuild + `dotnet build`；`tools/构建3FP.ps1` 负责依赖准备与门禁 |

## 3. 阶段 0：架构调查（真实调用链）

### 3.1 解复用与音频解码

- `PlayerSession::PumpPlayback()` 读取包并驱动播放循环（`FFF.Native/3FP/Core/PlayerSession.cpp:4036,4133`）。
- `DecodePacket()` 送入 FFmpeg 并接收 `AVFrame`（`:4536-4577`）。
- `QueueAudioFrame()` 把 stream time_base 的 PTS 转成 100ns 媒体位置，叠加外部音轨 offset 或 audio delay，过滤 seek 前帧，然后调用 renderer（`:5486-5519`）。

### 3.2 PCM、重采样与输出

- `PlayerWasapiRenderer::Enqueue()` 是唯一稳定的“解码后、重采样前”的音频入口（`FFF.Native/3FP/Audio/WasapiRenderer.cpp:577`）。
- 在该入口内：`SelectBedRoute()` 决定床声道路由，`EnsureResampler()` 建立/复用 swr，唯一设备侧重采样点是 `:719 swr_convert`。
- 解码帧可为任意 FFmpeg packed/planar sample format、采样率与声道布局；IAMF 输出为 packed S16，可到 7.1.4（12 声道）。
- 现有 Audio Render Plug **不能**作为通用 PCM tap：`SelectBedRoute()` 会排除立体声源和多声道端点，它只覆盖“多声道源到立体声端点”的双耳折叠场景；且它替换 PCM，不是旁路。

### 3.3 媒体时钟与时间轴

- 音频存在时主时钟为 `audioRenderer_->Position100ns()`（`PlayerSession.cpp:6728-6745`）；无音频时退到 QPC。
- WASAPI 时钟由 `IAudioClock::GetPosition`、已提交帧数与 QPC 插值构成（`WasapiRenderer.cpp:1141-1168`）。
- 视频在 `PumpVideoPresentation()` 中跟随该时钟显示或丢帧（`PlayerSession.cpp:4638-4649`）。

### 3.4 生命周期

- `DoSeek()` flush 解码器、重置时钟与 audio renderer，并推进 `timelineGeneration`（`:5990-6100`）。
- 换音轨最终复用 seek/reset 路径（`:6135-6187`）。
- 暂停后工作线程不再 pump playback；停止/关闭先停 renderer 再释放解码器（`:6238-6241`）。

### 3.5 字幕与 UI

- 定时字幕图层：`FFF.Player/UI/播放器字幕叠加呈现器.vb`。
  - 构造注入 `快照提供器`、`字幕提供器`、`提交图层`（`:120-163`）。
  - 渲染泵 `刷新计时器_Tick` 在后台线程运行，不依赖 UI 消息队列（`:144-150,187`）。
  - 关键门槛：`If 快照 Is Nothing OrElse 快照.视频宽度 = 0 OrElse 快照.视频高度 = 0 ... Then Return`（`:197-198`）。这正是“纯音频无封面”首版被排除的代码依据。
  - 已内置 `上一时间轴代次 / 时间轴代次有效`（`:46-47`），说明 seek 后的时间轴失效在托管侧已有先例。
- 信息图层（调试 OSD）同样由独立定时器驱动，且已用 `安全获取(...)` 包装所有提供器（`FFF.Player/UI/播放器信息图层呈现器.vb:62-68,92-113,237-238`），可作为状态显示的先例。
- 设置与快捷键体系集中在 `FFF.Player/设置.vb`、`Form设置_*.vb`、`播放器画面菜单控制器.vb`；首版建议只用最小入口（菜单项 + 设置开关），不新增复杂 UI 框架。

## 4. 阶段 2 设计（已细化，待用户确认决策点后落码）

> 完整实现分析（挂点 file:line、数据流、ABI 导出草案、worker 协议、线程边界、分阶段门禁）见 **`IMPLEMENTATION_ANALYSIS.md`**。本节仅保留原则摘要。

### 4.0 实现主线一句话

内核 `Enqueue()` 顶部旁路原始解码帧进 SPSC 环（tap 状态属 PlayerSession，渲染器可被替换）；宿主 pump 线程经新增 pull 导出把块写进独立 Python ASR 进程的 stdin；结果 JSON 走 stdout 回传；字幕经字幕呈现器零订阅者的 `绘制扩展定时文字` 事件 append 命令注入——零 ABI 改动、零新 D3D 层。时间轴全程媒体 100ns + `timelineGeneration` 代次。

### 4.1 ASR 引擎抽象

统一接口（无论后端是独立 Python 子进程还是 llama-server）：

- `初始化(模型路径, 设备, 语言)`：可能耗时数秒，必须在后台完成，失败只关闭字幕功能。
- `提交音频块(pcm16kMono, 起始100ns, 时长100ns, 时间轴代次)`：非阻塞入队。
- `取结果()`：返回 `{类型: 临时|确认, 文本, 起始100ns, 结束100ns, 时间轴代次}`。
- `取消/重置(时间轴代次)`：seek、换轨、切文件、停止时调用。
- `释放()`。

要点：

- 时间戳一律来自媒体侧（tap 携带的 100ns 与时长），不得使用模型返回时间或系统墙钟。
- 第一版不做后端并存；接口保留替换余地。

### 4.2 音频 tap 设计原则

- 挂在 `WasapiRenderer::Enqueue()` 入口、`SelectBedRoute()` 之前，保证拿到解码后的完整声道，不受床路由/折叠影响。
- 热路径只做最小工作：引用帧并写入单生产者单消费者有界环形缓冲；16 kHz 单声道转换、下混、去重大块交给消费侧。
- 缓冲必须有界并允许丢弃：消费慢时丢旧块并计数，绝不反压播放器。
- **保留全部有效声道**，不默认只取前两声道。5.1/7.1 的对白常在中置声道，截前两声道会改变 ASR 语义。默认环形缓冲 32 MiB，可配置更大。
- 每块携带：`起始100ns`、`时长100ns`、`采样率`、`声道布局`、`sample_format`、`时间轴代次`。

### 4.3 外部可达性（必须显式确认）

若 tap 只留在 `FFF.Native` 内部，则外部进程/托管侧无法取得数据。当前不存在把音频帧送出的既有导出或函数表，因此**必须新增可选导出或函数表**，并作为一项明确的设计决策记录：

- 方案一：pull 型导出，外部按块拉取。
- 方案二：回调/函数表，内核在数据就绪时回调。

二选一需要结合播放器线程模型决定，且必须保持“内核不等待外部”。

### 4.4 字幕状态机

- 临时字幕：随分块结果更新，可原地修正；仅在文本变化时发布，且发布频率上限 10 Hz。
- 确认字幕：窗口右边界离开重叠区、或检测到稳定边界后固化，立即发布。
- seek/换轨/切文件：用 `时间轴代次` 作废全部临时与在途结果，清空显示，不跨代次拼接文本。
- 暂停：停止推进窗口，不清除已确认条目。
- 停止/关闭：释放 worker 与模型进程。

## 5. 阶段门禁

进入阶段 2（改播放器代码）必须全部满足：

1. `RUNTIME_EVALUATION.md` 给出首选后端，且该后端在本机完成模型加载、中文与英文样本识别、2/5/10 秒分块与连续运行实测。
2. 记录模型加载时间、RTF、峰值显存/内存/CPU、积压行为、错误恢复行为。
3. 明确“首字延迟”的测量口径；若当前接口无法拿到增量首字时刻，必须写明上限而非估算。
4. 明确 tap 的外部可达路径（新增导出/函数表的具体方案）。
5. 明确首版排除项与理由（纯音频无封面、倍速、词级时间戳）。

当前状态（2026-10-22）：

| # | 项 | 状态 |
|---|---|---|
| 1 | 首选后端 = 原生 Transformers `-hf`（GPU）；中文 2/5/10s 分块实测完成；英文短样本识别完成 | ✅ |
| 1 | 10–15 分钟连续运行实测 | ⏸ **待补**：两次尝试均在权重加载后静默崩溃（疑显存超额订阅，桌面应用占 ~7GB/8GB）。用户指示暂缓实机测试 |
| 2 | 加载 5.43s / RTF 0.23–0.50 / 峰值显存 ~1.6GB / RSS ~1.97GB | ✅（连续运行的积压与错误恢复行为随项 1 补测） |
| 3 | 首字延迟口径 = 整段处理耗时上限（接口无增量输出） | ✅（`RUNTIME_EVALUATION.md` §3.2） |
| 4 | tap 外部可达路径 = 新增 pull 导出族（`FFF3FP_EnableAudioTap/ReadAudioTap/DisableAudioTap/GetAudioTapStats`，独立附加头，size/version 门控） | ✅（`IMPLEMENTATION_ANALYSIS.md` §4.4） |
| 5 | 排除项与理由 | ✅（§1） |

任一未满足，则继续停留在阶段 1，不动内核或播放器业务代码。**项 1 的连续运行补测完成前，不落码**；用户已确认先进入设计阶段（`IMPLEMENTATION_ANALYSIS.md`），补测与落码顺序由用户决定。

## 6. 风险登记

| 风险 | 现状 | 处置 |
|---|---|---|
| C 盘空间被 GPU 版 PyTorch 占满（剩余约 0.4 GB） | 已发生 | 所有模型、缓存、临时目录改 E 盘；后续安装显式指定目标盘 |
| 英文 15 分钟连续测试静默崩溃（疑显存超额订阅） | 待复测 | 用户指示暂缓实机测试；崩溃根因隔离脚本已备（最小加载+单次推理），补测时先跑它 |
| llama.cpp 路线未实测 | 进行中 | cudart.zip 下载至 143/404MB 被中断；未出实测前不得作为结论。静态结论：权重 1.46→0.95 GiB、预期 decode 2–4×，依据见 `MODEL_PERFORMANCE_ANALYSIS.md` §5 |
| 模型非原生流式 | 已确认 | 应用侧做分块/重叠/去重，并在文档中说明不是原生流式 |
| 词级时间戳缺失 | 已确认 | 首版用窗口边界；后续可选 ForcedAligner |
| 纯音频无封面无字幕图层 | 已确认 | 首版排除；后续复用歌词节拍 |
| GPU 显存与播放争用 | **根因已定性** | 两次静默崩溃 = WDDM 超额订阅（桌面占 5.6–7.2G/8G + bf16 需 1.8G ⇒ 可分配 ~1.0G）；处置 = Q8_0 量化（0.95G 权重，总预算 ~1.2G）+ 开启前 VRAM 探测，见 `MODEL_PERFORMANCE_ANALYSIS.md` §4/§7 |
