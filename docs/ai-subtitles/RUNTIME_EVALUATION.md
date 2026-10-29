# Qwen3-ASR-0.6B 在本机的运行时评估


> ⚠️ **路线已变更（2026-10-27）**：本文写于 llama.cpp 时代（`llama-server` 子进程 + GGUF 双文件）。
> 推理栈现已换成 **sherpa-onnx（ONNX Runtime）进程内**：后端只剩 `cpu` / `cuda` 两档 + `npu` 占位，
> 模型是一套 ONNX 目录而非 .gguf，扩展包清单升到 **v2**（见 `PACK_FORMAT.md`）。
> **已失效**：llama-server 各后端的实测（启动耗时、VRAM、RTF）
> **仍有效**：Qwen3-ASR-0.6B 本身的 RTF 量级与模型尺寸对比
> 归并理由与新方案全貌见主仓 `docs/交接文档-2026-10-27-设备选择统一方案.md`。

> 目的：在修改播放器之前，确认“模型本身能做什么”“官方接口能做什么”“某个运行时在 Windows 上实际能做什么”“应用自己分块能做什么”这四件事，并把它们严格分开。  
> 所有本机数值都来自 `tools/ai-subtitles/qwen3_asr_benchmark.py`，结果 JSON 落 E 盘；未实际运行的项目一律标注为“未实测”。

## 1. 本机验证环境

| 项 | 实测值 |
|---|---|
| 操作系统 | Windows 11，10.0.26300 |
| CPU/GPU | NVIDIA GeForce RTX 4060 Laptop GPU，8 GB，驱动 616.92 |
| 托管 Python | WorkBuddy 隔离环境 `C:/Users/20210/.workbuddy/binaries/python/envs/default`，Python 3.13.14 |
| 推理依赖 | `torch 2.14.1+cu132`（官方 Windows CUDA 13.2 轮）、`transformers 5.20.2`、`accelerate 1.15.0`、`librosa 1.0.0`、`soundfile 0.14.0` |
| CUDA | `torch.cuda.is_available()` 为真；`torch.version.cuda` 为 13.2；bf16 支持为真 |
| 磁盘 | C 盘被 GPU 版 PyTorch 占用后剩余约 0.4 GB；模型与缓存全部改到 `E:/WorkBuddy/3FP-AI-Subtitles` |
| 模型缓存 | `HF_HOME`、`TORCH_HOME`、`TMP/TEMP`、`PYTHONPYCACHEPREFIX` 均指向 E 盘，避免继续消耗 C 盘 |

C 盘吃紧是本次实验产生的真实环境风险，后续若继续在本机安装大依赖，必须显式指定 E 盘路径。

## 2. 模型与来源核对

### 2.1 官方 `-hf` 原生 Transformers 模型

来源：ModelScope 镜像 `https://www.modelscope.cn/Qwen/Qwen3-ASR-0.6B-hf.git`，提交 `005b749cd1566dc579fe0238316087e2fc1945c0`；仓库头部元数据为 `license: apache-2.0`、`library_name: transformers`、`pipeline_tag: automatic-speech-recognition`，语言列表包含 zh/en/yue 等。

本机下载文件与哈希：

| 文件 | 大小（字节） | SHA-256 |
|---|---:|---|
| `model.safetensors` | 1,564,928,088 | `d3f212dd20abecd315d830bc54ae3865e56ebfc3276484e57b771288ba27fd35` |
| `tokenizer.json` | 11,429,653 | `fe1fad59be22a41ee293363fcf95fdedbc7c93f3b49270b1d2e18bd1399a7a05` |

两个哈希与仓库 LFS 指针中的 `oid sha256` 完全一致。

官方模型卡关键事实：

- Qwen3-ASR 原生进入 Transformers，起点是 **v5.13.0**；本机 5.20.2 满足。
- 推荐入口是 `processor.apply_transcription_request(...)`，随后 `model.generate(...)`，并用 `processor.decode(..., return_format="parsed")` 取 `language` 与 `transcription`。
- 原始输出形如 `language Chinese<asr_text>...`。
- `language` 支持全名或 ISO 码；不传则为自动语种检测。
- `prompt` 可作为上下文/热词偏置。
- 时间戳不是 ASR 主模型自带能力，需要单独的 `Qwen3-ForcedAligner-0.6B-hf`（NAR，11 种语言，官方称最多约 5 分钟语音）。
- 官方 README 明确：流式推理目前只在 vLLM 后端提供，且流式不支持批量与时间戳。
- 评测栏给出 Open ASR Leaderboard 上 `Qwen3-ASR-0.6B-hf` 平均 WER 6.31。

本机核对过的接口签名（真机读取）：

```text
Qwen3ASRProcessor.apply_transcription_request(
    audio: ndarray | Tensor | 序列,
    language: str | list[str] | None = None,
    prompt: str | list[str] | None = None,
    **kwargs
) -> BatchFeature
```

即：**可以直接吃 numpy 数组，不必须先落 WAV 文件**。这对播放器集成是决定性的，因为 tap 出来的是内存 PCM。

### 2.2 官方 GGUF 模型

来源：ModelScope 镜像 `https://www.modelscope.cn/ggml-org/Qwen3-ASR-0.6B-GGUF.git`，提交 `86222636b790fae434fb24c03c46fc817dfb27b2`；模型卡片写的用法是 `llama-server -hf ggml-org/Qwen3-ASR-0.6B-GGUF`，base model 为 `Qwen/Qwen3-ASR-0.6B`。

本机下载文件与哈希：

| 文件 | 大小（字节） | SHA-256 |
|---|---:|---|
| `Qwen3-ASR-0.6B-Q8_0.gguf` | 804,749,248 | `bca259818b50ca7c4c05e9bdb35a5dc04fa039653a6d6f3f0f331f96f6aa1971` |
| `mmproj-Qwen3-ASR-0.6B-Q8_0.gguf` | 214,392,480 | `41a342b5e4c514e968cb756de6cd1b7be39eff43c44c57a2ef5fc6522e36603d` |

两个哈希与 LFS 指针一致。

### 2.3 llama.cpp 预编译件

本机使用官方 nightly `b11669`（`v0.7.0` 发布正文指向该 nightly），Windows x64 资源：

| 包 | 字节 | SHA-256 |
|---|---:|---|
| `llama-b11669-bin-win-cuda-13.4-x64.zip` | 154,582,227 | `3b92d5c064f5032e603e531d3ec8961f3bd6613e5dfad047399a92dec8e95dbc` |
| `cudart-llama-bin-win-cuda-13.4-x64.zip` | 423,535,356 | `738f8c251ac22b70c3ae6f83a10cf222725df0395246a2cf58f32bdb85fbe668` |
| `llama-b11669-bin-win-cpu-x64.zip` | 19,537,886 | `37b3efcf962847ca869745fd4ccf7f5e7e8dacfeff9fa3523b4ca8cdc66d632b` |

### 2.4 官方 Python 包 `qwen-asr`

PyPI 元数据（本机查询）：当前 `0.0.6`，发布于 2026-01-30，Apache-2.0，纯 Python 通用轮；硬性依赖 `transformers==4.57.6`、`nagisa`、`soynlp`、`accelerate==1.12.0`、`qwen-omni-utils`、`librosa`、`soundfile`、`sox`、`gradio`、`flask`、`pytz`；`vllm==0.14.0` 为可选 extra。

结论：`qwen-asr` 把 Transformers 锁在 4.57.6，而 `-hf` 原生支持要求 `transformers>=5.13.0`。两条路线互斥，不能靠同一个环境同时满足。

### 2.5 测试素材

| 文件 | 属性 | 大小（字节） | SHA-256 |
|---|---|---:|---|
| `samples/asr_zh.wav` | pcm_s16le，16 kHz，单声道，4.2039375 s | 134,570 | `46dbc998c9d1d48111267c40741dd3200f2e5bcf4075f8c4c97f4451160dce50` |
| `samples/asr_en.wav` | pcm_s24le，48 kHz，单声道，15.051229 s | 2,172,458 | `f9b4440ac8393e47c14a6240e9739dea09b645bb1592b8f2dd48feb9666cea7f` |

两者都是官方模型卡示例使用的同一批样本 URL，因此中文样本可对照官方预期文本「甚至出现交易几乎停滞的情况。」，英文样本可对照 “Mr. Quilter is the apostle of the middle classes, and we are glad to welcome his gospel.”。

## 3. 路线 A：原生 Transformers `-hf`（Windows GPU）

### 3.1 实测结果（本机，CUDA，bf16）

运行：

```bash
python tools/ai-subtitles/qwen3_asr_benchmark.py \
  --model <E:/.../Qwen3-ASR-0.6B-hf> \
  --audio <E:/.../samples/asr_zh.wav> \
  --output <E:/.../results/transformers_gpu_zh.json> \
  --durations 2 5 10 --device cuda --dtype bfloat16
```

结果文件：`E:/WorkBuddy/3FP-AI-Subtitles/results/transformers_gpu_zh.json`

| 指标 | 实测 |
|---|---|
| 模型加载耗时 | 5.43 s |
| 加载阶段进程峰值 RSS | 1,308,200,960 B（约 1.22 GiB） |
| 加载阶段峰值 GPU 显存占用 | 7,237 MiB（注意：这是整卡口径，含本机其它进程） |
| warmup（2 s 音频）耗时 | 2.12 s |
| warmup 输出 | `language Chinese<asr_text>甚至出现交易。` |
| 2 s 音频推理 | 0.675 s，RTF **0.338** |
| 5 s 音频推理 | 1.136 s，RTF **0.227** |
| 10 s 音频推理 | 4.982 s，RTF **0.498** |
| 2/5/10 s 的 torch 峰值分配 | 1,597,287,424 / 1,616,325,120 / 1,656,305,152 B |
| 进程峰值 RSS（推理阶段） | 约 1.97 GB |

识别正确性：中文样本自动检测为 `Chinese`，输出「甚至出现交易几乎停滞的情况，……」，与官方模型卡预期文本一致。10 s 用例因源样本只有 4.2 s，程序按文档说明对源音频做了重复拼接，因此文本出现重复片段；这属于测试构造，不是模型错误。

### 3.2 已确认能力

- Windows x64 原生可用，CUDA 可用，bf16 可用。
- 可直接输入 numpy 数组，不需要中间文件。
- 自动语种检测可用；可强制指定语言。
- 单次 `generate` 是**完整生成后一次性返回**，当前基准拿不到首个 token 的到达时刻，所以“首字延迟”目前只能给出上限（等于整段处理耗时）。

### 3.3 已确认限制

- 这不是原生流式接口。当前实现下，要得到连续字幕必须由应用自己做分块、重叠与去重。
- 没有内置词级时间戳；若将来要精确词级时间，需要额外的 ForcedAligner 模型，且它也只是 NAR 后处理。
- RTF 随窗口长度非线性上涨：10 s 窗口 RTF 比 5 s 更差，说明窗口不能随便加长。

## 4. 路线 B：llama.cpp + GGUF（Windows GPU/CPU）

状态：模型与预编译件已下载并校验哈希，基准尚未运行。当前**没有任何本机实测数值**，因此不作任何性能结论。

待验证项：

- `llama-server` 或等价工具能否在 Windows 上加载上述 GGUF 与 mmproj。
- 音频输入是文件还是可喂内存 PCM；若只能文件，则播放器侧要额外落盘。
- 是否具备可用的增量输出语义，还是只能整段返回。
- 停止、取消、重置是否能满足播放器 seek 场景。
- CUDA 13.4 预编译件在本机驱动 616.92 下是否正常运行。
- CPU 回退的 RTF 是否可接受。

## 5. 路线 C：vLLM 流式

官方唯一明确支持流式的后端。当前**未安装、未验证**，且官方安装示例面向 Linux/CUDA 12.9 nightly 索引。按用户要求，不得假定它能在原生 Windows 直接使用；除非后续在 WSL2/Linux 环境实测，否则不作为首选。

## 6. 否决记录

- **`qwen-asr` 0.0.6 与 `-hf` 原生路线同环境互斥**：前者锁 `transformers==4.57.6`，后者要求 `>=5.13.0`。首版不引入该包。
- **FlashAttention 2**：官方建议安装，但它在 Windows 上通常需要源码构建，且只对长输入/大 batch 有意义；首版单路实时不需要，不引入。
- **Gradio / Flask 演示组件**：不属于播放器功能，不引入。
- **ForcedAligner**：首版不做，字幕时间轴改用媒体侧分块边界，而不是模型词级时间戳。
