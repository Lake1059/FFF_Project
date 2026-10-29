# 推理后端矩阵：N 卡 / I 卡 / A 卡 / 纯 CPU 分别怎么跑


> ⚠️ **路线已变更（2026-10-27）**：本文写于 llama.cpp 时代（`llama-server` 子进程 + GGUF 双文件）。
> 推理栈现已换成 **sherpa-onnx（ONNX Runtime）进程内**：后端只剩 `cpu` / `cuda` 两档 + `npu` 占位，
> 模型是一套 ONNX 目录而非 .gguf，扩展包清单升到 **v2**（见 `PACK_FORMAT.md`）。
> **已失效**：vulkan / rocm / sycl / OpenVINO 各后端的选型与 `-ngl` / `--device` 参数、按设备类别推断后端的整套逻辑
> **仍有效**：NVIDIA / Intel / AMD / CPU 硬件能力边界的一般结论
> 归并理由与新方案全貌见主仓 `docs/交接文档-2026-10-27-设备选择统一方案.md`。

配套代码：宿主 `FFF.Player/AI字幕/Ai字幕包.vb` + `Form设置_AI字幕.vb`，
worker `tools/ai-subtitles/asr_worker.py`。实测记录见 §6。

---

## 0. 结论先行

1. **llama.cpp 的一个二进制里只编进了它被编译时勾的那几个后端。** 所以"换显卡厂商"
   不是改个参数，而是**换一个 `llama-server.exe`**。这是整个设计的地基。
2. 于是 3FP 本体不猜设备、不写死厂商：包清单用 `servers` 声明"哪个后端用哪份二进制"，
   本体只让用户选**一类**设备（自动 / CPU / NVIDIA / Vulkan / AMD / Intel）。
3. **具体设备名不写死在代码里**：worker 启动时去问二进制自己（`--list-devices`），
   按前缀挑第 N 块。要不到就明确告警并回落，绝不静默跑在错的设备上。
4. **纯 CPU 是完整可用的兜底**，不需要额外的二进制：同一个 CUDA 构建加 `-ngl 0` 就能跑。
   本机实测（§6）：8/8 窗全出、文本与 GPU 臂逐窗一致，代价是 rtf 从 ~0.038 涨到 ~0.245
   —— 仍然比实时快 4 倍以上。

---

## 1. 后端 × 硬件对照

| 设置项里的选项 | 后端 | 适用硬件 | 依赖的运行库 | 本机状态 |
| --- | --- | --- | --- | --- |
| 自动 | auto | 让二进制自己挑 | — | ✅ 实测 |
| CPU | cpu | 任何 x86-64，**包括没有可用 GPU / 驱动太老的机器** | 无（ggml-cpu 自带多档 ISA） | ✅ 实测 |
| NVIDIA 显卡 | cuda | NVIDIA（Kepler 之后） | 驱动自带 CUDA 运行时 | ✅ 实测（RTX 4060 Laptop） |
| 通用显卡 | vulkan | N / A / I 三家通吃 | 显卡驱动自带 Vulkan | ⚠️ 未实测（本机无第二张卡） |
| AMD 显卡 | rocm | AMD（HIP） | ROCm / HIP SDK（Windows 面比 Linux 窄） | ⚠️ 未实测 |
| Intel 显卡 | sycl | Intel Arc | oneAPI DPC++ 运行时 / Level Zero | ⚠️ 未实测 |

**Vulkan 的价值**：一个二进制走天下，给"不知道用户是什么卡"的分发包用最省事；
代价通常是吞吐低于原生后端（CUDA/ROCm）。

---

## 2. 两个旋钮：`-ngl` 与 `--device`

worker 拼出来的命令行长这样（实测日志原文）：

```
llama-server.exe -m <model.gguf> --mmproj <mmproj.gguf>
  --port 8199 --host 127.0.0.1 --ctx-size 4096
  `--n-gpu-layers 999        # 0 = 全部留在 CPU；999 = 能 offload 的层全给设备
  --threads 8               # CPU 后端时决定速度，自动取 min(核数,16)
  --temp 0.6 --no-cache-idle-slots --cache-ram 0
  --device CUDA0            # 非 auto 后端才出现；CPU 臂是 --device none
```

| 用户选择 | `--device` | `--n-gpu-layers` |
| --- | --- | --- |
| 自动 | 不传（二进制自选） | 999；若二进制报不出任何 GPU 设备则 `none` + 0 |
| CPU | **`none`** | **0** |
| NVIDIA / Vulkan / AMD / Intel | `<前缀><序号>`，序号留空取第 0 块 | 999 |

⚠️ **`-ngl 0` 不等于"不碰显卡"**：官方 build 文档明确说 `--n-gpu-layers 0` 仍可能有部分
算子走 GPU。要彻底把 GPU 排除在外，必须同时给 `--device none`。本机实测该参数被接受、
模型正常加载、出字正常（§6），所以"选 CPU"这条路是干净的。

`--device` 的值**不是我们拼出来的**，是 `--list-devices` 报出来的原样字符串。
后端 → 前缀的匹配给了别名表，避免吃版本差异：

```python
BACKEND_ALIASES = {
    "cuda":   ("cuda", "nvidia"),
    "vulkan": ("vulkan",),
    "rocm":   ("rocm", "hip", "amd"),   # 历史上既报过 ROCm 也报过 HIP
    "sycl":   ("sycl", "intel"),
    "cpu":    ("cpu",),
}
```

---

## 3. 三家显卡各自怎么落地

### 3.1 NVIDIA（CUDA）

1. 取一份 CUDA 构建的 `llama-server`（本机 `llama-b11669/cuda/`，随包带 `ggml-cuda.dll`
   与 cublas 的 DLL）。
2. 清单写 `"device": "cuda"`，`servers.cuda` 指向它。
3. 多卡时用户在设置里填"设备序号"（`CUDA0`/`CUDA1`/… 的第几块）。
4. 显存：Q8_0 实测增量 **1991 MiB**，Q6_K **1642 MiB**（RTX 4060 Laptop 8 GB）。

### 3.2 Intel Arc（SYCL）

1. 取 SYCL 构建（官方发行包名形如 `llama-b<build>-bin-win-sycl-x64`）。
2. 装 Intel oneAPI DPC++ 运行时 / Level Zero（SYCL 构建的 DLL 依赖它，缺了会直接起不来）。
3. 清单写 `"device": "sycl"` + `servers.sycl`。
4. 若 SYCL 版跑不通，**退而求其次用 Vulkan 构建**（Arc 的 Vulkan 驱动是主推路径）。

### 3.3 AMD（Vulkan 优先，ROCm 次之）

1. Windows 上先试 **Vulkan 构建**：AMD 驱动自带 Vulkan，无需额外 SDK，最省事。
2. 确实要 ROCm/HIP 的话，需要 HIP SDK 且显卡 gfx 架构在支持列表内；Windows 侧支持面
   比 Linux 窄，出问题就回 Vulkan。
3. 清单 `servers` 里可以**同时放 `vulkan` 和 `rocm` 两份**，用户切一下就能对比。

### 3.4 共通的坑：多模态（mmproj）不一定每个后端都完整

Qwen3-ASR 是"主权重 + mmproj 音频塔"。**`--n-gpu-layers` 只 offload 主权重时各后端
都很成熟，音频塔在 Vulkan / SYCL / ROCm 上的支持历史上有缺口**。我们没有 I 卡和 A 卡，
无法实测，所以：

- 选了非 CUDA 后端后，**第一件事是看 `ai-subtitles.log`**：worker 会把
  `--list-devices` 的结果、最终 `--device`、以及任何告警都写进去。
- 若该后端加载 mmproj 失败，llama-server 会直接退出，worker 报
  `llama-server exited early rc=...`，字幕功能静默关闭、**播放不受影响**。
  此时把设置切回 CPU 或 CUDA 即可。

---

## 4. 完全不支持 / 纯 CPU 怎么跑

不需要下载任何额外的东西。**任何 llama.cpp 构建都自带 CPU 后端**（`ggml-cpu-*.dll`，
运行时按 CPU 挑最优 ISA 档：sse42 / haswell / skylakex / icelake / alderlake / zen4 …）。
所以最老到只支持 SSE4.2 的机器也有对应的档位，只是慢。

做法：`-ngl 0 --device none`，所有权重留在内存，线程数拉到 `min(核数, 16)`。

实测（本机 16 线程，同一份 CUDA 二进制 + `-ngl 0`，见 §6）：

- 首窗就绪 ~11 s（含模型加载），8/8 窗全部出字；
- rtf 0.119–0.373（中位 ~0.245）→ **5 秒窗口的推理耗时 0.6–1.9 s**，
  比实时快 4 倍以上，跟上播放毫无压力；
- 常驻内存约 1 GB（Q8_0）。

CPU 上的调参建议（都在设置页"高级选项"里）：

| 症状 | 建议 |
| --- | --- |
| 字幕明显滞后 | 把**步长**调小（如 3 s）让窗口更密，或把窗口调大到 8 s 减少单位时间推理次数 |
| 风扇狂转、机器卡 | 这是 `--threads 16` 的必然结果；CPU 臂就是要吃满核，介意的话只能用 GPU 臂 |
| 起不来 | 看日志里的 `missing ...gguf` / `llama-server exited early`，多半是模型或二进制路径 |

---

## 5. 在 3FP 里怎么指定

设置 → **AI 字幕** 页：

- **推理设备**（主行，下拉）：自动（跟随扩展包）/ CPU（不用显卡）/
  NVIDIA 显卡（CUDA）/ 通用显卡（Vulkan）/ AMD 显卡（ROCm/HIP）/ Intel 显卡（SYCL）
- **设备序号**（高级选项里）：多显卡时选第几块，留空 = 由 llama-server 自选
- 状态行会告诉你真正的落点，例如：

  - `推理设备：NVIDIA 显卡（CUDA）（第 1 块）`
  - 包没带对应二进制时转**橙色**：
    `注意：包内没有该后端专用的二进制，会回落到包默认的那个。`

优先级：**用户选择 > 包清单的 `device` > auto**。切换会立即重启 worker，不影响播放。

---

## 6. 实测记录（2026-10-23，本机 RTX 4060 Laptop 8 GB）

素材 42.6 s 中英混合 mp4，窗口 5 s / 步长 5 s，Qwen3-ASR-0.6B Q8_0，
llama.cpp b11669（commit 620d2225c）。同一份 CUDA 二进制跑两臂。

|  | CUDA 臂 | CPU 臂 |
| --- | --- | --- |
| 实际参数 | `--n-gpu-layers 999 --device CUDA0 --threads 8` | `--n-gpu-layers 0 --device none --threads 16` |
| 启动 → llama-server ready | ~12 s | ~9 s |
| 出窗数 | 8/8 | 8/8 |
| rtf（推理/音频时长） | 0.024 – 0.077 | 0.119 – 0.373 |
| rtf 中位 | ~0.038 | ~0.245 |
| 识别文本 | 中英自动判别全对 | 与 CUDA 臂**逐窗一致**（个别语气词 1 字差，如「嗯哼」/「嗯嗯」） |
| 上屏 | `字幕[代次 0]` 8 条，呈现帧数 125→220→420→518 | `字幕[代次 0]` 8 条，呈现帧数 197→381→589 |

日志直接引用（CPU 臂）：

```
[01:19:00] AI 字幕：推理后端 cpu（用包默认二进制）
[01:19:01] [asr-worker] 设备=CPU（-ngl 0 --device none）；该二进制可用设备: CUDA0
[01:19:11] [asr-worker] final [0.16s..5.16s] lang=chinese rtf=0.252 :: 甚至出现交易几乎停滞的情况。嗯哼。
```

**CPU 臂是"完全不支持的机器"的忠实模拟**：`-ngl 0` 之后 CUDA 后端不再参与任何计算，
走的确实是 ggml-cpu。

---

## 7. 还没验证的

1. **Vulkan / ROCm / SYCL 三个后端本机都没有**（只有一张 N 卡），只验证了
   "选中它们 → worker 正确探测 → 发现二进制里没有 → 告警并回落"这条路径，
   **没有验证它们真能跑 Qwen3-ASR**。
2. **多模态 mmproj 在非 CUDA 后端上的完整性**未验证（§3.4）。
3. **真正的多显卡**（`--device CUDA1`）未验证，只验证了序号越界时的告警回落。
4. CPU 臂的线程数（16）没有做过 A/B，只是取 `min(核数,16)`；低核数机器上的
   表现未测。
