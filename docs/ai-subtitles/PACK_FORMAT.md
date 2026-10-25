# AI 字幕扩展包格式（PACK_FORMAT v1）

> 日期：2026-10-23 ｜ 分支 `feature/realtime-ai-subtitles-qwen3-asr`
> 本文是**本体与包之间的唯一契约**。3FP 本体不内置任何 ASR 能力、不认识任何模型名或量化档——
> 它只认"一个含 `ai-subtitles.json` 的目录"。换模型、换量化、换推理后端，都是换包或改包的事。

---

## 1. 最小结构

```
我的AI字幕包\
  ai-subtitles.json     ← 必须有。没有它，这个目录就不是包
  asr_worker.py         ← worker 脚本；文件名随意，由清单的 worker 字段指定
```

包是**目录**，不是压缩包、不是单个文件。设置页的浏览按钮选的是目录（也可以直接选
`ai-subtitles.json`，本体自动取其所在目录）。

其他东西（模型 .gguf、llama-server.exe、python）都可以放在包外，用绝对路径指过去；
想做成便携包就把它们拷进包目录，清单里写相对路径。**两种混用也行**（比如模型在包内、
llama-server 用机器上那一份）。

---

## 2. 清单字段

| 字段 | 类型 | 必填 | 含义 |
| --- | --- | --- | --- |
| `version` | 整数 | 否 | 清单版本，当前 `1`。缺了按 1 处理 |
| `name` | 字符串 | 否 | 包显示名，设置页状态行用。缺省取目录名 |
| `worker` | 路径 | **是** | worker 脚本（.py）。本体给它喂 stdin 帧、读 stdout JSON 行 |
| `python` | 路径 | 否 | python 可执行文件。留空 = PATH 上的 `python` |
| `llamaServer` | 路径 | 否 | `llama-server.exe`。留空 = PATH 上找 |
| `servers` | 对象 | 否 | 后端 → `llama-server.exe` 的映射，键取 `cuda`/`vulkan`/`rocm`/`sycl`/`cpu`。**换后端要换二进制**，见 §2.3 |
| `device` | 字符串 | 否 | 包推荐的后端（`auto`/`cpu`/`cuda`/`vulkan`/`rocm`/`sycl`）。用户在设置里另选时以用户为准 |
| `model` | 路径 | 否 | 主权重 .gguf。留空 = worker 按 `--model-dir` + 默认量化档自己拼 |
| `mmproj` | 路径 | 否 | 投影权重。留空 = 与主权重同目录的 `mmproj-*.gguf`；**旁边也没有 = 单文件模型**（不传 `--mmproj`，由 llama-server 自己判定能不能加载） |
| `models` | 对象 | 否 | **模型档位**：档名 → `{model, mmproj}`。同一包带标准档与低配档，用户在设置里换档，见 §2.4 |
| `layers` | 整数 | 否 | 模型层数（GGUF `block_count`）。**只用于生成设置页「GPU 层数」下拉的预置档**：自动 → 全部 N 层 → 每 4 层一档递减 → 0 层；缺省 = 旧的 28 层固定表，行为与过去一致，见 §2.6 |
| `llamaArgs` | 数组 | 否 | **llama-server 附加参数**：`["--ctx-size","8192"]`，逐 token 追加到命令行。worker 固定默认值遇同名 flag 自动让位；设置页高级区的用户框排在其后、同名 flag 覆盖它，见 §2.5 |
| `window` | 秒 | 否 | 推理窗口，0.5–30，默认 5 |
| `step` | 秒 | 否 | 推进步长，0.5–30，默认 5。大于窗口时压到窗口 |

未知字段一律忽略（前向兼容）；`_说明` 之类下划线开头的键是给人看的注释。

### 2.1 路径解析规则

1. **绝对**（含盘符或 `\\` 开头）→ 原样使用；
2. **相对** → 相对**包根目录**解析（不是相对 3FP 程序目录）；
3. **留空** → 走 fallback（见上表）；
4. 分隔符正反斜杠都接受，本体统一按 Windows 路径处理。

### 2.2 相对路径是"便携包"的关键

```json
{
  "version": 1,
  "name": "Qwen3-ASR-0.6B（Q6_K，便携）",
  "worker": "asr_worker.py",
  "python": "runtime/venv/Scripts/python.exe",
  "llamaServer": "runtime/llama-server.exe",
  "model": "models/Qwen3-ASR-0.6B-Q6_K.gguf",
  "mmproj": "models/mmproj-Qwen3-ASR-0.6B-Q6_K.gguf",
  "layers": 28
}
```

整个目录拷到另一台机器上就能用（前提：那台机器有 NVIDIA 卡且驱动支持 CUDA）。

不想手写相对路径的话，用 `tools/ai-subtitles/make_portable_pack.py`：它读现有清单，
把 worker / 模型 / llama-server 拷进目标目录，再写出相对路径的清单（`--dry-run` 先看体积）。

### 2.4 `models`：一个包可以带好几档权重

低性能设备（无独显、内存吃紧）需要更小的权重，但"换包"成本太高。所以允许一个包
同时带多档，用户在 **设置 → AI 字幕 → 高级 → 模型档位** 里选：

```json
"models": {
  "Q8_0":   { "model": "models/Qwen3-ASR-0.6B-Q8_0.gguf",   "mmproj": "models/mmproj-Qwen3-ASR-0.6B-Q8_0.gguf" },
  "Q4_K_M": { "model": "models/Qwen3-ASR-0.6B-Q4_K_M.gguf", "mmproj": "models/mmproj-Qwen3-ASR-0.6B-Q8_0.gguf" }
}
```

| 规则 | 行为 |
| --- | --- |
| 档名 | 任取（建议用量化名），界面上原样显示 |
| 任一路径不存在 | **整档丢弃** —— 宁可少一个选项，也不让用户选到注定起不来的档 |
| 省略 `mmproj` | 取主权重旁边第一个 `mmproj-*.gguf`；旁边也没有 = 该档按单文件模型处理（不传 `--mmproj`） |
| 旧设置里的档名在新包里不存在 | 回落默认档并写日志，**绝不拒绝启动** |
| 没有 `models` | 下拉只剩"默认档"且置灰（旧包完全不受影响） |
| 默认档 | 仍是顶层 `model` / `mmproj`；`models` 只是额外选项 |

各档体积、显存与选型见 `MODEL_TIERS.md`。

### 2.3 `servers`：一个包可以带好几份 llama-server

llama.cpp 的**一个二进制里只编进了它被编译时勾选的那几个后端**。本机的 CUDA 构建
跑 `--list-devices` 只报出 `CUDA0`：

```
Available devices:
  CUDA0: NVIDIA GeForce RTX 4060 Laptop GPU (8187 MiB, 7106 MiB free)
```

所以"换个厂商"不是改个参数，而是**换一个 llama-server 二进制**。清单用 `servers`
把这件事说清楚：

```json
{
  "llamaServer": "runtime/cuda/llama-server.exe",
  "device": "cuda",
  "servers": {
    "cuda":   "runtime/cuda/llama-server.exe",
    "vulkan": "runtime/vulkan/llama-server.exe",
    "sycl":   "runtime/sycl/llama-server.exe"
  }
}
```

规则：

| 情况 | 行为 |
| --- | --- |
| 用户在设置里选的后端在 `servers` 里 | 用那一份二进制 |
| 不在（或整张表没写） | 回落到 `llamaServer`，设置页状态行变橙提示"这个包没带该后端的 llama-server" |
| 选 **CPU** | 不需要专用二进制 —— 任何 llama.cpp 构建都自带 CPU 后端，回落过去照样能跑（`-ngl 0 --device none`），所以不提示 |

⚠️ **`llamaServer` 必须始终指向一份能跑的二进制**：用户选「自动」（或清单 `device` 是
`auto`）时走的是它，不会去 `servers` 里挑。惯常写法是让它跟 `servers` 里的默认后端那一份
相同（见上面示例）。`servers` 只负责"用户显式选了某类显卡"的分支。

各后端对应的硬件与运行状态见 `BACKEND_MATRIX.md`。

### 2.5 `llamaArgs`：换模型时的参数逃生门

worker 有一份固定默认参数（`--ctx-size 4096` / `--temp 0.6` / 缓存策略，依据见
`MODEL_TIERS.md` §7）。**换一个不是 Qwen3-ASR 的模型时这些默认值未必还合适**——
所以清单可以用 `llamaArgs` 逐 token 追加自己的参数：

```json
"llamaArgs": ["--ctx-size", "8192", "--temp", "0.4", "--flash-attn"]
```

规则：

| 规则 | 行为 |
| --- | --- |
| 同名 flag | worker 的固定默认值**整项让位**（不是依赖"后者覆盖"的语义——某些 llama-server 版本对重复 `--ctx-size` 直接报错）。可覆盖 `--ctx-size` / `--temp` / `--n-gpu-layers` / `--threads` / `--device` 等全部长选项 |
| 新 flag | 原样追加在命令行末尾（`--flash-attn` 这类开关） |
| 格式非法 | 整项不是数组就整个忽略；单个元素不是字符串就跳过那一个——**绝不让坏参数拖垮启动** |
| token 语义 | 一律原样传递：这些是命令行 flag 与值，不是路径。需要路径型参数（`--grammar` 等）请写绝对路径 |
| 只认长选项 | 让位匹配按 `--flag` / `--flag=value` 做。写短选项（`-c 8192` / `-ngl 16`）不触发让位，会与 worker 默认值重复传参，某些 llama-server 版本直接报错——**逃生门里只写长选项** |
| 固定段不在让位范围 | `-m`（主权重）/ `--mmproj` / `--port` / `--host` 由本体和 worker 固定拼接，`llamaArgs` 里写了会重复传参（`--port` 重复 = health timeout），**勿写** |
| flag 必须成对 | `["--ctx-size"]` 这种悬空 flag 本体不拦（原样传递的代价），到 llama-server 启动才报错——报错可读（stderr 落盘 `llama-server.log`），但别这么写 |

典型用途：换的模型上下文要得更大（`--ctx-size 8192`）、换的权重训练时温度不同
（`--temp`）、后端专属开关。**每一项都请先想清楚"它改变哪个结论"**——本项目
`MODEL_TIERS.md` §7 逐条记录了默认值的实测依据，逃生门是给"有依据的例外"
用的，不是给"据说更好"用的。

#### 用户侧覆盖：设置页的「llama-server 附加参数」文本框

清单之外，**设置 → AI 字幕 → 高级区**还有一个多行文本框（界面标签
「llama-server 附加参数」，设置项 `Ai字幕附加参数`），临时调参不必改包。写法：
**每行一个 token**——`--ctx-size` 与 `8192` 分两行写，或单行 `--ctx-size=8192`；
空行（含只有空格的行）忽略；`#` 开头的行是注释，不传给 llama-server。

合并规则（宿主 `Ai字幕管道.合并附加参数`）：**两个来源都传，用户参数排在包参数
之后**；用户点名过的 flag（`--flag` 或 `--flag=value` 形式取 `--flag` 部分），
连同它在包参数里的值（紧随的下一个不以 `--` 开头的 token）一起被剔除——即
「用户覆盖包」，避免 llama-server 收到重复 flag（某些版本对重复 `--ctx-size`
直接报错）。这与 worker 侧固定默认值让位（`_user_first`）是同一套语义。

| 规则 | 行为 |
| --- | --- |
| 生效时机 | **启动期参数**：改内容后重启 worker 才生效（与模型档/后端/语言同一重启判定队列） |
| 传递形态 | 合并后的每个 token 以**等号形式** `--llama-arg=<token>` 传给 worker——argparse 的 append action 不收以 `--` 开头的值，拆两个 token 传会被判 expected one argument 直接 exit 2（2026-10-25 实测） |
| 没选包时 | 文本框置灰，水印提示「需先选择有效扩展包（含 ai-subtitles.json）」 |
| 日志 | 合并结果记一行：`AI 字幕：llama-server 附加参数 N 条（清单 X + 用户 Y，同名 flag 以用户为准）`，N 按 token 数计 |

换模型还要过三道架构约束（`model` / `mmproj` 怎么给、API 模型名怎么探测、
输出格式怎么解析），完整说明见 `USER_GUIDE.md` §7。

### 2.6 `layers`：让「GPU 层数」预置跟着模型走

「GPU 层数」下拉过去是一张写死的 28 层表（28/24/20/16/12/8/4/0）——因为
Qwen3-ASR-0.6B 恰好 28 层。换个层数不同的模型，这张表就名不副实了：清单可以用
`layers` 把层数告诉本体，由它生成预置档。

```json
"layers": 32
```

它**不改任何推理行为**，只决定下拉里出现哪些档位。生成规则（N = 声明值，
**缺省不写 = 28**）：

| 档位 | 怎么来 |
| --- | --- |
| 自动（全部卸载） | 永远在第一位：不向 llama-server 传 `--n-gpu-layers`，由它自行全卸 |
| 全部 N 层 | 显式传 `--n-gpu-layers N` |
| N-4 / N-8 / … | 每 4 层一档递减，直到 0 |
| 0 层（纯 CPU） | 永远在最后一位 |

N=28 时生成的正是旧固定表——**缺省不写 `layers` 就是过去的行为**，旧包一个字
不用改。字段缺失或类型不符（写成字符串等）按没声明处理，坏数据不会让包不可用。

老设置里存过的任意层数归位：不在当前预置表里的值，≥ N 记为 N，其余归「自动」
（只改内存、不主动落盘，用户一动界面就写回）——界面显示什么，实际就是什么。

---

## 3. 校验与失效

载入时按序检查，任何一步不过就返回**一句人话错误**，设置页状态行变红显示它：

| 检查 | 错误文案要点 |
| --- | --- |
| 目录为空/不存在 | 请先选择扩展包目录 |
| 目录里没有 `ai-subtitles.json` | 该目录不是 AI 字幕扩展包（缺少 ai-subtitles.json） |
| JSON 解析失败 | 清单不是合法 JSON |
| `worker` 缺失或文件不存在 | 清单未指定 worker / worker 文件不存在 |
| `model`/`python`/`llamaServer` 指了不存在的文件 | 对应文件不存在（原样带路径） |
| `mmproj` 指了不存在的文件 | 投影权重不存在（原样带路径）。**注意与"留空"区分**：留空走单文件模型 fallback，写了路径但文件不在才是配错 |
| `window`/`step` 超出 0.5–30 | 窗口/步长超出范围 |

**失效行为**：功能自动停用，播放完全不受影响。修好后重新选一次目录即可（设置页状态行
实时校验，选中即生效）。包目录被删、模型被挪走也是同一条路径。

---

## 4. 换量化档 / 换模型

改 `model` 指到哪个 .gguf 就行——本体不需要知道 Q8_0 与 Q6_K 的区别：

```json
"model": "models/Qwen3-ASR-0.6B-Q6_K.gguf"
```

实测显存增量（同一张卡、同一会话口径）：Q8_0 **1991 MiB**、Q6_K **1642 MiB**（省 349 MiB）。
识别质量见 `CER_BASELINE_RESULTS.md`：Q6_K 与 Q8_0 实词一致率 100%。

---

## 5. worker 协议（包作者需要知道的）

本体 → worker 走 stdin，worker → 本体走 stdout，都是二进制帧 + JSON 行：

- **入**（每块）：`[u32 LE metaLen][meta JSON][PCM]`
  meta 含 `generation / position100ns / frames / sampleRate / channels / positions /
  sampleFormat / bytes`（`positions` 是每个槽位的 FFmpeg AVChannel 值，下混用它挑对白声道）；
- **出**（每行一个 JSON）：`{"type":"status",...}`、
  `{"type":"final","generation":..,"start100ns":..,"end100ns":..,"text":"..",
  "language":"Chinese","target":"Chinese","matched":true}`、`{"type":"stats",...}`。

`language` 是模型自报的语言名；`target` / `matched` 只在宿主传了 `--language` 时有意义，
worker 只上报、不自己丢结果——"只显示所选语言"的丢弃策略在宿主侧（见
`LANGUAGE_SUPPORT.md`）。`auto` 时不传 language 字段，`target` 为空串、`matched` 恒 true。

协议在两种推理后端下完全一致（D7 的"路线不影响接口"）。`generation` 变化即 seek/切轨，
worker 应丢弃在途窗口。

worker 由本体托管：BelowNormal 优先级、无窗口、stdin 关闭后 2 秒未退出即 Kill 整棵进程树。
**worker 崩溃只关字幕，播放不受影响**（P5 已实测）。

---

## 6. 设置页的两个覆盖项

| 设置项 | 默认 | 含义 |
| --- | --- | --- |
| 窗口秒覆盖 | 0（跟随清单） | 非 0 时强行覆盖 `window`，用于调参 |
| 步长秒覆盖 | 0（跟随清单） | 非 0 时强行覆盖 `step` |

覆盖项存在是为了**不为了试参数去改包**；日常使用应保持 0。
