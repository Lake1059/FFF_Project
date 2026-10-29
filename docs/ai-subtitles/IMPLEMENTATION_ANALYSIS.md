# 3FP 实时 AI 字幕：落地实现分析


> ⚠️ **路线已变更（2026-10-27）**：本文写于 llama.cpp 时代（`llama-server` 子进程 + GGUF 双文件）。
> 推理栈现已换成 **sherpa-onnx（ONNX Runtime）进程内**：后端只剩 `cpu` / `cuda` 两档 + `npu` 占位，
> 模型是一套 ONNX 目录而非 .gguf，扩展包清单升到 **v2**（见 `PACK_FORMAT.md`）。
> **已失效**：worker 内部的 llama-server 启动、HTTP 转写、参数合并
> **仍有效**：音频 tap 挂点、媒体时间切窗、ABI 纪律、线程边界
> 归并理由与新方案全貌见主仓 `docs/交接文档-2026-10-27-设备选择统一方案.md`。

> 分支：`feature/realtime-ai-subtitles-qwen3-asr`（worktree `C:/PLAN/3FCompare/_3fp-ai-subtitles`，基线 `1a930b0`）
> 总纲：`MASTER_PLAN.md`（决策清单/路线图/门禁）；配套：`PROJECT_PLAN.md`（范围与门禁）、`RUNTIME_EVALUATION.md`（运行时实测）
> 本文只回答一个问题：**在 3FP 现有代码里，实时 ASR 字幕具体挂在哪里、怎么流、谁来管生命周期**。所有结论带 file:line 证据；未落码。

---

## 1. 结论摘要

**一条主线、三个进程角色、五个挂点**：

- **取数**：内核 `PlayerWasapiRenderer::Enqueue()` 顶部（`SelectBedRoute()` 之前）旁路一份原始解码帧进 SPSC 环形缓冲。这是全链唯一“解码后、任何变换前”的点，保全部声道、避 plug 折叠。
- **搬运**：宿主一个后台线程经**新增 pull 导出** `FFF3FP_ReadAudioTap()` 把块写进 ASR worker 进程的 stdin（长度前缀帧）。
- **识别**：独立 Python 进程做 16k 单声道转换、分窗、推理，结果以 JSON 行走 stdout 回传。
- **显示**：宿主订阅字幕渲染器**零订阅者的扩展事件** `绘制扩展定时文字`，在事件里向命令集合追加文字命令——同一帧、同一图层、同一提交，**零 ABI 改动、零新 D3D 层**。
- **时间轴**：一切用媒体 100ns 位置 + `timelineGeneration` 代次；墙钟只用于统计，不进字幕。

播放器侧总增量：内核 2 个新文件 + 4 处小改；宿主 2 个新文件 + 4 处小改。ASR 失败的任何形态（进程起不来、推理崩、显存不足）都只关字幕，不碰播放。

---

## 2. 总体数据流

```
 ┌──────────────────── 播放器进程（FFF.Player.exe）────────────────────┐
 │                                                                    │
 │ FFmpeg decode                                                       │
 │   └─ PlayerSession::QueueAudioFrame()          PlayerSession.cpp:5486│
 │        └─ PlayerWasapiRenderer::Enqueue(frame, position100ns)        │
 │             :577                                                    │
 │             ├─【T1】AsrAudioTap::Produce(frame, position, gen)  ← 新 │
 │             │      memcpy → SPSC ring（满则丢最旧，计数）            │
 │             └─ SelectBedRoute() → swr_convert → WASAPI → 喇叭 :585   │
 │                                                                    │
 │  pump 线程 ──FFF3FP_ReadAudioTap()──▶ stdin 管道 ──┐                │
 │  result 线程 ◀──stdout 管道──── JSON 行 ◀──────────┤                │
 │       └─▶ Ai字幕状态机（并发队列 + 代次作废）       │                │
 │                                                                    │
 │  PrecisionTimer worker（字幕呈现器后台线程）                         │
 │       └─ 生成命令() → RaiseEvent 绘制扩展定时文字   呈现器.vb:331     │
 │            └─【T5】Ai字幕状态机.取当前条目() → append 文字命令        │
 │                 → 图层签名去重 → 提交图层 → D3D present               │
 └────────────────────────────────────────────────────────────────────┘
                    │ stdin/stdout 管道（JSON + 长度前缀 PCM）
 ┌──────────────────┴─────────────────── ASR worker 进程（Python）─────┐
 │  收帧 → 16k 单声道转换（numpy）→ 按媒体时间分窗（5s/5s）             │
 │       → transformers 推理 → {"type":"final","gen":N,                │
 │         "start100ns":S,"end100ns":E,"text":"...","language":"zh"}   │
 │  见 generation 变化 → 丢弃全部累积状态（seek 作废）                   │
 └────────────────────────────────────────────────────────────────────┘
```

---

## 3. 已核验的代码事实（file:line 证据）

| # | 事实 | 证据 | 对设计的含义 |
|---|---|---|---|
| F1 | `Enqueue()` 是唯一“解码后、重采样前”音频入口，且 `SelectBedRoute()` 在同一函数顶部先跑 | `WasapiRenderer.cpp:577,585` | tap 必须插在 `:585` 之前，否则拿到的是 plug 折叠后的立体声 |
| F2 | 音频位置（100ns）由 session 侧算好后传入：`QueueAudioFrame()` 叠加 external offset / audio delay、过滤 seek 前帧，再调 `Enqueue(frame, position)` | `PlayerSession.cpp:5486-5519` | tap 拿到的 `position100ns` 就是媒体位置；无 PTS 帧为 `AV_NOPTS_VALUE`（`:589` 有既有兜底算法） |
| F3 | 渲染器是 `unique_ptr`，设备切换/独占模式回退时**整体替换** | `PlayerSession.cpp:1824-1843`、`:1234`、`:3408` | tap 状态不能活在渲染器里；必须属 PlayerSession |
| F4 | “跨渲染器存活的会话级状态”有现成范式：`PlayerAudioRuntimeState*` 由 session 持有、渲染器只存非拥有指针 | `WasapiRenderer.h:84-96` 注释、`:189` | `AsrAudioTap` 照抄这个模式（`SetAsrTap()`，session worker 线程设置，同 `SetAudioRenderPlug` 的先例 `:128-137`） |
| F5 | `timelineGeneration` 只在真 seek 成功后自增，存在快照里，注释明说“Overlay producers use it to discard old state” | `PlayerSession.cpp:6073`；`FFF.Player.Api.h:259-261` | 代次就是给叠加层用的作废信号；tap 块盖代次章、seek 时清 ring |
| F6 | 字幕渲染器有一个**零订阅**的扩展事件，在 `生成命令()` 内、SRT/弹幕命令生成完之后触发，参数带 `命令` 集合、`播放位置`、`视频区域` | `播放器字幕叠加呈现器.vb:165,331-333`；事件参数类 `:636-648` | 注入路径：向 `e.命令` append 文字命令即同帧同图层渲染 |
| F7 | 图层提交带 FNV 签名去重；文本/显隐变化→签名变→重提交 | `:344-349,372-409` | AI 字幕显隐由签名机制自动处理，无需自己管提交 |
| F8 | 纯音频无封面天然被排除：`快照.视频宽度 = 0 OrElse 视频高度 = 0` 直接 Return | `:197-198` | 符合首版范围；AI 字幕继承该门槛，无需额外代码 |
| F9 | seek 代次作废在托管侧有先例（danmu 调度器重置），且 `生成命令` 已经接收 `时间轴代次` 参数 | `:313-317`、`:278,342` | 事件参数加一个 `时间轴代次` 属性即可透传（内部类，零 ABI 影响） |
| F10 | 宿主装配中心在 `Form1.vb:133` 创建字幕呈现器；歌词是“外部定时文本源 + 独立设置项 + 开关”的完整范式 | `Form1.vb:133-136,144-148`；`设置.vb:62 启用歌词支持` | AI 字幕的开关/设置/装配照歌词范式抄 |
| F11 | 跨 PE 音频消费者 ABI 有现成纪律：独立附加头、POD + size/version、**禁止 FFmpeg 类型过界**（AVChannel 用 int32 传）、tail-append 导出、不动快照、不 bump PlayerApiVersion | `FFF.Player.AudioRenderPlug.h:1-17,49-56` | tap 导出头照抄这份纪律 |
| F12 | 播放控制器集中了打开/停止/跳转/快照读取 | `播放器控制器.vb:487,805,829,240` | worker 生命周期挂这些点 |

---

## 4. 原生内核侧设计

### 4.1 `AsrAudioTap`（新文件 `FFF.Native/3FP/Audio/AsrAudioTap.{h,cpp}`）

职责只有一个：**热路径最小工作**。

```
Produce(const AVFrame* frame, int64 position100ns, uint64 generation) noexcept
```

- 在 `Enqueue()` 顶部、`SelectBedRoute()` 之前调用（F1）。关闭时 `asrTap_ == nullptr`，一次指针负载，零成本（对照 plug 的“seam 计数器只在安装后动”原则）。
- 内部：有界 SPSC 环形缓冲，块 = 定长头 + PCM 字节（**原样拷贝**，不重采样、不下混、不动格式）。满 → 丢最旧块并 `++droppedBlocks`。
- 块头字段（ POD，过导出时同构 ）：

| 字段 | 类型 | 说明 |
|---|---|---|
| `generation` | u64 | session 代次盖章（F5） |
| `position100ns` | i64 | 帧媒体位置；`AV_NOPTS_VALUE` 时消费侧按累积帧数推算 |
| `frames` | u32 | 帧数 |
| `sampleRate` | u32 | 解码帧采样率 |
| `channels` | u32 | 声道数（≤ 24，对齐 `PlayerAudioRuntimeState::MaximumChannels`） |
| `positions[24]` | i32 | AVChannel 值直传（F11 纪律），-1 = 无 |
| `sampleFormat` | i32 | AVSampleFormat 值直传 |
| `bytes` | u32 | PCM 字节数 |

- `NoteGeneration(generation)`：session 在 `++snapshot_.timelineGeneration`（`:6073`）处调用；**清空 ring**。seek 前的块不可能被 pump 取走——内核侧保证，不依赖宿主/worker 自觉。
- 线程：生产 = session worker（调 `Enqueue` 的那一个）；消费 = 宿主 pump 线程。SPSC 无锁；导出侧加一把轻量 mutex 防宿主并发调 `ReadAudioTap`（低频，不进热路径）。

### 4.2 渲染器挂载（`WasapiRenderer.{h,cpp}` 小改）

```cpp
void SetAsrTap(AsrAudioTap* tap) noexcept;   // session worker 线程调，非拥有
```

`Enqueue()` 内在 `:585` 前：

```cpp
if (asrTap_ != nullptr) asrTap_->Produce(frame, position100ns, tapGeneration_);
```

`tapGeneration_` 由 session 经 `SetAsrTap` 之外的一个 `NoteGeneration` 直写渲染器（或 tap 自己持有代次原子量、session 直接写 tap——后者更少穿一层，采用后者）。

### 4.3 PlayerSession 挂载（`PlayerSession.{h,cpp}` 小改）

- 成员 `std::unique_ptr<AsrAudioTap> asrTap_`（默认 null = 功能关）。
- 打开媒体且 AI 字幕启用 → 建 tap；关闭/停止 → 销毁。设备切换**不碰** tap（F3/F4）。
- `DoSeek()` 里 `++snapshot_.timelineGeneration` 旁：`if (asrTap_) asrTap_->NoteGeneration(snapshot_.timelineGeneration);`

### 4.4 导出 ABI（新文件 `FFF.Native/3FP/Api/FFF.Player.AudioTap.h`）

照抄 plug 头纪律（F11）：独立附加头、无快照字段、无 offset pin、不 bump `PlayerApiVersion`、tail-append。

```c
#define FFF3FP_AUDIO_TAP_VERSION 1u

// 配置：环容量（字节，默认 32 MiB）、flags 预留（v1 全 0）。
struct FFF3FPAudioTapConfig {
    std::uint32_t size;      // sizeof 自报
    std::uint32_t version;   // FFF3FP_AUDIO_TAP_VERSION
    std::uint32_t ringCapacityBytes;
    std::uint32_t flags;
};

// 块元信息 + 实际写入字节数。capacity 不足 → BufferTooSmall，requiredBytes 回填。
struct FFF3FPAudioTapBlock {
    std::uint32_t size;
    std::uint32_t version;
    std::uint32_t bytesWritten;
    std::uint32_t requiredBytes;   // capacity 不足时为目标大小
    std::uint64_t generation;
    std::int64_t  position100ns;
    std::uint32_t frames;
    std::uint32_t sampleRate;
    std::uint32_t channels;
    std::int32_t  positions[24];
    std::int32_t  sampleFormat;
};

// 统计：丢弃块数、生产块数、当前缓冲字节。信息图层/日志用。
struct FFF3FPAudioTapStats {
    std::uint32_t size;
    std::uint32_t version;
    std::uint64_t blocksProduced;
    std::uint64_t blocksDropped;
    std::uint64_t bufferedBytes;
};

// 三个导出（PlayerApi.cpp 尾部追加，FFF3FP_API 约定同既有）：
FFF3FP_API FFFResult FFF3FP_EnableAudioTap(FFF3FPHandle player,
    const FFF3FPAudioTapConfig* config) noexcept;
FFF3FP_API FFFResult FFF3FP_DisableAudioTap(FFF3FPHandle player) noexcept;
// 非阻塞：ring 空 → Success 且 bytesWritten = 0。
FFF3FP_API FFFResult FFF3FP_ReadAudioTap(FFF3FPHandle player,
    void* dst, std::uint32_t capacity, FFF3FPAudioTapBlock* block) noexcept;
FFF3FP_API FFFResult FFF3FP_GetAudioTapStats(FFF3FPHandle player,
    FFF3FPAudioTapStats* stats) noexcept;
```

**为什么是 pull 而不是共享内存直连 worker**（决策 D3）：

| | pull + 管道（选定） | shm 直连 |
|---|---|---|
| 生命周期 | 全绑宿主；宿主死则 worker 管道断裂自退 | shm 需自己防泄漏（worker 崩溃后内核对象残留） |
| 可调试性 | worker 是纯 stdin/stdout 程序，可手工喂帧复现 | 必须起全链路 |
| 拷贝次数 | 2 次（Enqueue→ring→管道缓冲），384KB/s～2.3MB/s 量级，可忽略 | 1 次 |
| 宿主控制力 | 暂停/切文件时宿主停 pump 即可 | worker 直接读内核 ring，宿主只能干瞪眼 |

v2 若实测管道成瓶颈再上 shm（导出面已按“块”抽象，换传输不动内核）。

---

## 5. 托管宿主侧设计（VB.NET）

### 5.1 `Ai字幕管道.vb`（新，`FFF.Player/AI字幕/`）——worker 进程管理

- **启动**：`播放器控制器.打开媒体()`（`:487`）且 `设置.启用Ai字幕` 且媒体有音轨 → `Process.Start(pythonExe, asr_worker.py --model <目录>)`，`UseShellExecute=False`，重定向 stdin/stdout/stderr，`PriorityClass=BelowNormal`。
- **pump 线程**：循环 `ReadAudioTap` → 非空则写 stdin（`[u32 metaLen][meta JSON bytes][PCM bytes]`）；空则 `Sleep(1)`。见 `generation` 变化只可能是内核已清 ring 后的新块，无需处理。
- **result 线程**：`stdout.ReadLine` → JSON 解析 → `Ai字幕状态机.提交结果(entry)`。pipe EOF / 进程退出 → 记日志、通知状态机“worker 没了”、关字幕显示，**播放继续**。
- **停止**：`播放器控制器.停止()`（`:805`）、关闭媒体、切文件、退出 → 关 stdin → 等 2s → Kill。worker 侧 stdin EOF 即优雅退出。
- **Python 运行时**：实验分支要求独立 venv，README 写明 `pip install torch --index-url https://download.pytorch.org/whl/cu132 transformers soundfile psutil`（实测版本组合见 `RUNTIME_EVALUATION.md` §1）。播放器不内置 Python，不做安装器。

### 5.2 `Ai字幕状态机.vb`（新）——结果管理与显示决策

- 并发队列收 result 线程的条目；每条目 = `{类型, 代次, 起始100ns, 结束100ns, 文本, 语言}`。
- **显示策略 latest-wins +  staleness guard**（决策 D6，见 §7）：
  - 当前条目 = 最近收到的非空文本条目；
  - 守卫：条目.结束100ns + 2×窗口 < 快照.播放位置 → 视为落后过多，隐藏；
  - 代次变化（`e.时间轴代次`，F9）→ 清空当前条目与在途结果；
  - 文本为空 → 隐藏（静音段）。
- `取当前条目(播放位置 As TimeSpan) As String`：PrecisionTimer 线程每帧调，只读内存，不锁推理。
- 异常自闭：所有 public 方法 try/catch，绝不把异常抛进字幕呈现器（虽然 `提交当前帧` 外层有兜底 `:350-352`，但被兜住意味着整帧图层不提交，外部字幕也没了，所以状态机必须自己不抛）。

### 5.3 字幕注入（`播放器字幕叠加呈现器.vb` 小改 + `Form1.vb` 小改）

- 事件参数类加一个属性（内部类，零 ABI 影响）：`Friend ReadOnly Property 时间轴代次 As ULong`，由 `生成命令` 已有的 `时间轴代次` 参数透传（`:331-332` 处构造实参多传一个）。
- `Form1.vb:133` 创建呈现器后：`AddHandler 字幕图层呈现器.绘制扩展定时文字, AddressOf OnAi字幕绘制扩展`。
- 事件处理（PrecisionTimer worker 线程）：

```vb
Private Sub OnAi字幕绘制扩展(sender As Object, e As 定时文字图层绘制事件参数)
    Try
        Dim 文本 = _ai字幕状态机.取当前条目(e.播放位置, e.时间轴代次)
        If String.IsNullOrWhiteSpace(文本) Then Return
        Dim 区域 = e.视频区域
        Dim 行高 = 区域.高度像素 / 22.0F
        Dim 有外字幕 = _ai字幕配置.外部字幕已加载()   ' 播放控制器.当前字幕 IsNot Nothing
        Dim y = 区域.Y像素 + 区域.高度像素 - If(有外字幕, 行高 * 2.2F, 行高 * 1.2F)
        Dim cmd = New 定时文字命令()
        cmd.设置文字(文本, "微软雅黑", 行高,
                     New RectangleF(区域.X像素, y, 区域.宽度像素, 行高),
                     &HFFFFFFFFUI, &HI00000000UI, 行高 * 0.08F,
                     定时文字对齐.居中, 定时文字对齐.靠前)
        e.命令.Add(cmd)      ' 同一帧、同一图层；签名去重自动处理显隐
    Catch
        ' AI 字幕是可选图层：任何失败只当本帧没有 AI 字幕
    End Try
End Sub
```

约束（写进代码注释）：命令对象**每帧新建、append 后不得修改**——D3D present 线程会在提交期读它（`:14` 的类注释“调用方只能在同步提交期间读取命令”即此约束）。

### 5.4 设置与入口（`设置.vb` + 菜单，小改）

```vb
Public Property 启用Ai字幕 As Boolean = False
Public Property Ai字幕Python路径 As String = ""        ' 空 =  PATH 上的 python
Public Property Ai字幕模型目录 As String = ""
Public Property Ai字幕窗口秒 As Double = 5.0
Public Property Ai字幕步长秒 As Double = 5.0
```

菜单：标题栏菜单加“AI 实时字幕”复选项（挂 `播放器画面菜单控制器` 同一模式，`Form1.vb:159`）；状态提示走信息图层 `显示操作信息`（`Form1.vb:164` 先例）。

---

## 6. ASR worker 协议（`tools/ai-subtitles/asr_worker.py`，新）

**stdin 帧**（宿主 pump 写）：`[u32 LE metaLen][metaLen 字节 UTF-8 JSON][PCM bytes]`

```json
{"generation": 12, "position100ns": 1234567890, "frames": 1024,
 "sampleRate": 48000, "channels": 6, "positions": [...],
 "sampleFormat": 8, "bytes": 24576}
```

**stdout 行**（worker 写，一行一个 JSON）：

| type | 载荷 | 时机 |
|---|---|---|
| `status` | `{"state":"loading\|ready\|error","detail":"..."}` | 启动加载模型（实测 5.4s）；加载失败 → 字幕功能关 |
| `final` | `{"generation":N,"start100ns":S,"end100ns":E,"text":"...","language":"zh"}` | 每个窗口推理完 |
| `stats` | `{"rtf":0.31,"windows":42,"dropped":0}` | 每 30s 一条，信息图层/日志 |

**worker 内部逻辑**：

1. 收帧 → 按 meta 转 16k 单声道 float32（numpy；v1 全声道平均——文档注明：5.1/7.1 对白在中置，v2 改按对话下混 C+L+R，benchmark 已证明 `apply_transcription_request` 直接吃 numpy 数组）。
2. 按**媒体时间**累积：`end100ns - 窗口起点 >= 步长` → 取最近 `窗口` 秒 → 推理 → 发 `final`，窗口右边界推进。
3. 媒体时间出现 > 2×窗口的跳空（暂停/seek 残留）→ 直接切窗，不跨跳空拼接。
4. `generation` 变化 → 丢累积缓冲（seek 作废；内核清 ring 保证不会收到旧块，双保险）。
5. 推理异常 → 发 `status error`，自身继续跑（下一个窗口再试）；连续 N 次失败 → 退出让宿主重启。
6. 临时目录：`--tmp-dir`（宿主传它验证过可写的根，通常是扩展包目录下的 `tmp`）→ 系统临时目录 → worker 脚本旁边的 `tmp` → cwd 的 `tmp`，逐级回退；每 worker 拿独立 `mkdtemp` 子目录，退出时整棵删掉。**不能没有这条链**：schtasks 派发等环境下系统临时目录可能整个不可用，Python 在 `mkdtemp` 直接 `FileNotFoundError` 退出（2026-10-23 完整跑实测，字幕全白）。宿主侧同时把子进程的 `TEMP`/`TMP` 指到同一根，llama-server 那层也有着落。

**v1 明确不做**：窗口重叠与跨窗文本去重（协议已带 `start/end`，将来加重叠只需改 worker 策略，宿主/内核不动）；词级时间戳；流式增量输出（Windows 原生路线无此能力，见 `RUNTIME_EVALUATION.md` §3.3）。

---

## 7. 时间轴、延迟与显示策略（用户红线专项）

**媒体时间戳来源链**（全程无墙钟）：解码帧 PTS → `QueueAudioFrame` 算 100ns（含 external offset / audio delay，`:5493-5503`）→ tap 块 `position100ns` → worker 窗口边界 → `final.start/end100ns` → 条目。墙钟仅用于 RTF 统计。

**端到端延迟账**（GPU 实测值，`transformers_gpu_zh.json`）：

| 环节 | 耗时 |
|---|---|
| 解码 → tap → 管道 | 实时 + ms 级 |
| worker 16k 转换 | ms 级 |
| 5s 窗口推理 | 1.14s（RTF 0.227）～2.49s（RTF 0.498） |
| 回传 + 下一帧呈现 | ≤ 17ms（60Hz） |
| **合计** | **约 1.2～2.7s，固定滞后** |

**显示策略**（因此字幕必然滞后于画面，这是批式推理的物理下限，不是 bug）：

- latest-wins：新条目到达即成为当前显示文本，显示到下一条到达；
- staleness guard：条目媒体结束时间 + 2×窗口 仍早于播放位置 → 隐藏（防推理卡住后字幕僵在屏上）；
- 代次变化 → 立即清空（seek 不等 worker 确认）；
- 真时间轴用途：条目的 `start/end100ns` 是**真实媒体时间**——首版即可顺手写 SRT 导出（后续功能，接口已备）。

---

## 8. 线程与锁边界

| 线程 | 做什么 | 禁止 |
|---|---|---|
| session worker（调 `Enqueue`） | tap memcpy + 原子索引 | 锁、分配、调外部 |
| 宿主 pump 线程 | `ReadAudioTap` + 管道写 | 碰 UI、碰状态机 |
| 宿主 result 线程 | 读管道 + 解析 + 入队 | 碰 UI |
| PrecisionTimer worker | 事件里读状态机 + 构造命令 | 阻塞、推理、抛异常 |
| D3D present 线程 | 读命令列表提交 | —（故命令 append 后禁改） |
| ASR worker 进程 | 转换 + 推理 | 无反压播放器的能力（也不需要） |

锁清单：ring 消费端 1 把轻量 mutex（仅宿主 pump 线程持）；状态机 1 把锁（result 线程写、timer 线程读，临界区 < 1μs）。**音频路径零新增锁**。

---

## 9. 分阶段落地与门禁

| 阶段 | 内容 | 通过门禁 |
|---|---|---|
| **2a 取数** | `AsrAudioTap` + 三个导出 + session/renderer 挂载 + vcxproj 登记 | 关闭时 `Enqueue` 成本 = 一次指针负载（A/B 同码率无差异）；开启后 dump 块验证：声道数/采样率/位置连续/代次盖戳正确；seek 后 ring 即空 |
| **2b 搬运与识别** | worker 脚本 + pump/result 线程 + 协议 | 手工向 worker stdin 喂录制的帧可复现识别；15 分钟连续运行无泄漏（worker RSS 平稳）、RTF 与 backlog 记录；worker 被 Kill 后播放无感 |
| **2c 显示** | 事件注入 + 状态机 + 设置 + 菜单 | 外部 SRT 加载/未加载两种情况下 AI 字幕不重叠、不闪；seek/暂停/停止/切文件全生命周期无僵死字幕；AI 推理崩溃只关字幕 |
| **2d 实机** | 真机端到端 + 文档 | 中文+英文素材端到端正确；延迟账实测；显存/CPU 占用记录 |

每阶段独立提交，任一阶段回归立即回退该提交（分支隔离，不碰 main）。

---

## 10. 风险登记

| 风险 | 等级 | 处置 |
|---|---|---|
| Python 运行时依赖重（torch ~2.5GB） | 中 | 实验分支 README 写清 venv 搭建；不做安装器；检测不到运行时 → 菜单项置灰 + 信息图层提示 |
| GPU 显存与播放争用（同卡） | 中 | RTF 0.23–0.5 有余量；设置可切 CPU 模式（worker `--device cpu`）；worker 优先级 BelowNormal |
| 模型加载 5.4s | 低 | 打开媒体即异步加载，`status` 事件驱动信息图层“AI 字幕加载中”；加载完才有字幕 |
| 批式推理固有 1.2–2.7s 滞后 | **已知物理下限** | latest-wins 显示策略 + 文档明示；v2 评估重叠窗口减半 |
| 全声道平均下混改变对白语义 | 低 | v1 文档注明；v2 按 C+L+R 对话下混（块头已带 `positions[]`，worker 侧可算） |
| 多字幕源视觉重叠（外部 SRT + AI） | 低 | AI 默认上移一行（§5.3）；设置可给垂直偏移 |
| 声道格式极其见（>24ch / planar 变体） | 低 | 块头 `channels ≤ 24` 与 `MaximumChannels` 对齐；超限帧 tap 跳过并计数（不阻塞播放） |
| pump 线程管道写阻塞（worker 卡死） | 低 | 写超时 → 判定 worker 异常 → Kill + 重启一次；再失败关字幕 |

---

## 11. 待用户确认的决策点

1. **窗口参数**：5s/5s（滞后 ~1.2–2.7s，RTF 余量大）还是 3s/3s（滞后 ~0.7–1.5s，精度下降、RTF 余量变小）？建议首版 5s/5s，协议已支持后续调。
2. **Python 运行时策略**：接受“实验分支自带 venv 搭建文档”，还是希望 Play 侧内置一个精简运行时目录（体积 +2.5GB）？
3. **显示层级**：AI 字幕与外部 SRT 同底并存（AI 自动上移）可否接受？还是首版强制“AI 字幕开启时暂停外部字幕渲染”？
4. **语言**：自动检测（实测可用）还是跟随系统/设置固定中文？影响 worker 参数一行。

以上任一调整都只影响对应模块，不动 §4 的数据流主线。
