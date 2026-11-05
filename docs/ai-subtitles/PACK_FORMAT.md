# AI 字幕扩展包格式（PACK_FORMAT v3）

> 日期：2026-10-29 ｜ 分支 `pr/ai-subtitles-qwen3-asr-20261023`
> 本文是**本体与包之间的唯一契约**。3FP 本体不内置任何 ASR 能力、不认识任何模型名、
> 不认识任何量化档——它只认"一个含 `ai-subtitles.json` 的目录"。

> ⚠️ **v1 已作废**。v1 是 llama.cpp 时代：`llama-server` 子进程 + GGUF 双文件，
> 后端是"编译时勾选的二进制"，所以清单要写 `servers` / `mmproj` / `llamaArgs` / `layers`。
> v1 清单**不再被接受**（载入时报错），迁移对照见 §7。
>
> **v2 仍被接受**（`version` 为 2 或 3 都行）：v2 是"只有 ONNX 一条腿"的形态。
>
> **v3 为什么又要 runtime**：v2 的前提是"推理全在 ONNX Runtime 里，后端只是 EP 名"。
> 实测推翻了这个前提——官方 int8 权重是 QOperator 动态量化（`DynamicQuantizeLinear`
> → `MatMulInteger`，激活量化成 uint8），CUDA 只注册 int8 版的 `MatMulInteger` 且
> 要求权重 zero_point 是标量，而本模型是 per-channel，于是整段**静默回落 CPU**。
> 同一份权重的 GGUF Q8_0（block-wise weight-only int8，反量化写在 kernel 里）在 CPU 上
> 比 sherpa 快 2.2–2.6 倍，三家 GPU 用一份 Vulkan 运行时就能吃。
> 于是 v3 = 一份 worker + 两种模型容器 + 每容器各自的运行时。

---

## 1. 最小结构

**llama 引擎（主形态，GGUF）**

```
我的AI字幕包\
  ai-subtitles.json          ← 必须有。没有它，这个目录就不是包
  asr_worker.py              ← worker 脚本；文件名随意，由清单的 worker 字段指定
  model\                     ← 模型目录（见 §2.3），也可以放包外写绝对路径
    Qwen3-ASR-0.6B-Q8_0.gguf           ← 主权重
    mmproj-Qwen3-ASR-0.6B-Q8_0.gguf    ← 音频编码器投影层（必需）
    selftest.wav             ← 可选，金样本（见 §2.6）
    selftest.txt             ← 可选
  runtime\
    vulkan\                  ← llama-server.exe + ggml-*.dll（见 §2.7）
```

**onnx 引擎（NPU 包与老包，ONNX 三件套）**

```
我的AI字幕包\
  ai-subtitles.json
  asr_worker.py
  qwen3_ort.py               ← NPU / Intel GPU 的逐段实现；缺它 ov-* 档全部跳过
  npu_precheck.py            ← 可选，一条命令查本机有没有 NPU（见 §4.3）
  model\
    conv_frontend.onnx
    encoder.onnx  (或 encoder.int8.onnx …)
    decoder.onnx  (或 decoder.int8.onnx …)
    tokenizer\               ← 目录，不是文件
      vocab.json
      merges.txt
      tokenizer_config.json
    selftest.wav / selftest.txt
  runtime\
    python\                  ← 便携 python（含 numpy / onnxruntime / sherpa-onnx / openvino）
    npu-cache\               ← 首次跑 NPU 时自动生成，是编译缓存，别手动删
```

`qwen3_ort.py` 必须与 `asr_worker.py` **同目录**：worker 靠 `import qwen3_ort`
拿三段推理。缺它时 `ov-npu` / `ov-gpu` / `ov-cpu` 会在 `_capability` 里被跳过，
只剩 CUDA 与 CPU —— 也就是退化成老包，而不是报错崩掉。

包是**目录**，不是压缩包、不是单个文件。设置页的浏览按钮选的是目录（也可以直接选
`ai-subtitles.json`，本体自动取其所在目录）。

模型目录与 python 都可以放在包外，用绝对路径指过去；想做成便携包就把它们拷进包目录，
清单里写相对路径。**两种混用也行**。

---

## 2. 清单字段

| 字段 | 类型 | 必填 | 含义 |
| --- | --- | --- | --- |
| `version` | 整数 | **是** | 清单版本，`3`（或旧包 `2`） |
| `name` | 字符串 | 否 | 包显示名，设置页状态行用。缺省取目录名 |
| `engine` | 字符串 | 否 | `llama` / `onnx`。**只是备注**：真正判据是模型目录里放的是 `*.gguf` 还是三个 `.onnx`，写错也不会把包判死，只会在日志里提一句 |
| `worker` | 路径 | **是** | worker 脚本（.py）。本体给它喂 stdin 帧、读 stdout JSON 行 |
| `python` | 路径 | 否 | python 可执行文件。留空 = PATH 上的 `python`。指向 `runtime/python/python.exe` 就是自带解释器（不分发依赖，见 §2.5） |
| `model` | 路径 | 否 | **模型目录**。留空 = 包目录下的 `model/`，见 §2.3 |
| `runtime` | 路径 | llama 必填 | llama 运行时目录（含 `llama-server.exe`）。留空时猜 `runtime/vulkan/`，见 §2.7 |
| `models` | 对象 | 否 | **模型档位**：档名 → 目录（或 `{"dir": 目录}`）。同一包带多套同引擎权重，见 §2.4 |
| `window` | 秒 | 否 | 推理窗口，0.5–30，默认 5 |
| `step` | 秒 | 否 | 推进步长，0.5–30，默认 5。大于窗口时压到窗口 |

未知字段一律忽略（前向兼容）；`_说明` 之类下划线开头的键是给人看的注释。

> **没有 `device` 字段了**：v1 里清单可以推荐后端（`auto`/`cuda`/`vulkan`…）。
> v2 把它交给用户的设置项（`Ai字幕运行设备`）——包不该预判"你这台机器跑在哪"，
> 那本来就是设备枚举能直接回答的事。推理档清单见 §4。

### 2.1 路径解析规则

1. **绝对**（含盘符或 `\\` 开头）→ 原样使用；
2. **相对** → 相对**包根目录**解析（不是相对 3FP 程序目录）；
3. **留空** → 走 fallback（见上表）；
4. 分隔符正反斜杠都接受，本体统一按 Windows 路径处理。

### 2.2 相对路径是"便携包"的关键

```json
{
  "version": 3,
  "name": "Qwen3-ASR-0.6B（GGUF Q8_0，便携）",
  "engine": "llama",
  "worker": "asr_worker.py",
  "python": "runtime/python/python.exe",
  "model": "model",
  "runtime": "runtime/vulkan"
}
```

整个目录拷到另一台机器上就能用：Vulkan 运行时覆盖 NVIDIA / AMD / Intel 三家显卡，
没有独显的机器自动落 CPU 档，**不需要为别人的机器改包**。

不想手写相对路径的话，用 `tools/ai-subtitles/make_portable_pack.py`：

```
# 先看它会拷什么、写出什么清单（不动磁盘）
python make_portable_pack.py --source E:/.../我的包 --target E:/.../便携包 --copy --dry-run

# 真拷（模型 + worker），python 环境另加 --copy-python
python make_portable_pack.py --source ... --target ... --copy
```

它会把 `worker` 拷到包根、`model` 拷成 `model/`、`models` 各档拷成 `models/<档名>/`、
`runtime` 整目录拷成 `runtime/<原名>/`、python 环境拷成 `runtime/python/`，然后写出一份
全部相对路径的清单。**模型目录缺文件直接拒绝打包并指缺哪个** —— 那种包在目标机上注定
起不来，早点发现比到现场排查便宜。GGUF 源缺 `runtime` 也一样拒绝（缺的正是
`llama-server.exe` 那套）。

### 2.3 模型目录：两套容器，各自成套

同一份 Qwen3-ASR 0.6B 权重有两种容器封装，**引擎按容器判**（见 §2.2 `engine`）：
目录里有 `*.gguf` 就是 llama 引擎，否则就是 onnx 引擎。

#### llama 容器：主权重 + 音频投影，两个都必需

| 件 | 作用 | 文件名 |
| --- | --- | --- |
| 主权重 | Q8_0 量化，约 972 MiB | `<任意名>.gguf`（如 `Qwen3-ASR-0.6B-Q8_0.gguf`） |
| 音频投影 | mmproj，音频编码器 + 投到 LM 的投影层 | 通常 `mmproj-*.gguf`，与主权重同目录 |

🔑 **`mmproj` 不是可选项。** 它是把音频特征喂进语言模型的桥；缺了它，
`llama-server` 能正常起来、能加载模型，但只认文字不认音频——送音频进去会得到
一句「我听不到」式的空转，而且**不报错**。所以校验把它与主权重同级硬卡：
缺主权重报「缺 GGUF 主权重」，缺 mmproj 报「缺音频投影（mmproj）」。

分词器**内嵌在 GGUF 里**，不需要单独的 `tokenizer/` 目录——这正是 GGUF 自描述的好处，
也是它比 ONNX 那套少一堆散件的原因。

区分规则只有一条：**名字里含 `mmproj` 的是投影，其余 `.gguf` 是主权重**（大小写不敏感）。
所以文件名不必严格叫某个名字，但别把 `mmproj` 三个字从投影文件名里去掉。
分片权重（`xxx-00001-of-00002.gguf`）会命中多个主权重，取排序第一片交给 llama.cpp 自己拼。
worker（`_llama_weights`）、宿主（`Ai字幕包.vb` 的 `找GGUF`）与打包工具（`llama_weights`）
三处口径一致。

#### onnx 容器（v2 沿用）：四件套

三个 ONNX + 一个 `tokenizer/` 目录。

| 件 | 作用 | 文件名 |
| --- | --- | --- |
| 卷积前端 | 把 128 维 fbank 特征降采样（约 42 MB） | `conv_frontend.onnx` |
| 编码器 | 音频编码器（int8 约 174 MB） | `encoder.onnx` 或 `encoder.<后缀>.onnx` |
| 解码器 | LLM + embedding（int8 约 721 MB） | `decoder.onnx` 或 `decoder.<后缀>.onnx` |
| `tokenizer/` | **目录**，内含 `vocab.json` / `merges.txt` / `tokenizer_config.json` | 名字固定 |

🔑 **文件名只钉主干，量化后缀随你。** 官方包叫 `encoder.int8.onnx` /
`decoder.int8.onnx`（量化写进文件名），换 int4 又变成 `.int4.onnx`。所以解析规则是：

1. 先找精确的 `<主干>.onnx`；
2. 没有就找 `<主干>.*.onnx`，**只允许命中一个**；
3. 一个都没有 → 报缺；命中多个 → 报「有多份，只能留一份」（一档一目录，本来就不该混）。

这条规则的价值在于**解压官方包就能直接当 `model/` 用**，不必先重命名两个文件。
worker、宿主（`Ai字幕包.vb` 的 `找权重`）与打包工具（`pick_onnx`）三处口径一致。

校验逐件做，缺哪件报哪件（例如「模型目录不合格：缺 decoder.onnx（也接受 decoder.int8.onnx 这类带后缀的）」）。

**从哪来**：官方现成包 `sherpa-onnx-qwen3-asr-0.6B-int8-2026-03-25`（约 838 MB）
解压即得上述结构，自带 `test_wavs/` 与 `transcript.txt`——拿一段来做 `selftest.wav` /
`selftest.txt` 正好。

⚠️ 官方产物是 **int8（W8A8，QOperator 形态：`DynamicQuantizeLinear` +
`MatMulInteger`）**。它能在 CPU / CUDA / DirectML / OpenVINO 上加载并出正确结果
（CPU 与 CUDA 有对应融合核；DirectML 走 DX12 通用路径）。**不要再转 int4**：
实测 int4（`MatMulNBits`）只省 28% 体积（+674 MB 独立目录换 −317 MB）、
CUDA 上仅追平 CPU、数值偏差约 20%，已判定不做 — 见
`交接文档-2026-10-27-设备选择统一方案.md` §13 与 §20。GGUF **不能**直接转成这套
ONNX，必须从 HF 原始权重重导。

### 2.4 `models`：**保留但一般不要用**

> ⚠️ 本项目的结论是：**只有一份权重 —— Qwen3-ASR 0.6B · int8 · 941 MB**。
> 因此设置页平时**不出现任何档位选择**：模型那一格只写事实（`int8 · 941 MB`），
> 不是一个永远只有一项的下拉。定了的事不摆成选项。

`models` 是给「同一个包里真并排放了两套完整权重」留的口子（比如以后官方出了
一份体积小得多、且实测值得付的量化），不是常规配置项。写了它，且确实有两档，
设置页才会出现下拉。格式：档名 → 目录，或 `{ "dir": "目录" }`，两种等价：

```json
"models": {
  "int8": "model",
  "另一档": "model-另一档"
}
```

| 规则 | 行为 |
| --- | --- |
| 档名 | 任取；若含 `int8` / `int4` / `fp16` / `fp32` 字样，**必须与目录里实际的 encoder 文件名一致**，否则整档丢弃（防「写着 int4 其实是 int8」，那种错不抛异常，只让人按错精度判断） |
| 任一路径不合格（不存在 / 缺文件） | **整档丢弃** —— 宁可少一个选项，也不让用户选到注定起不来的档 |
| 与 `model` 指同一目录 | 允许（就是「默认档也叫 int8」），零额外体积 |
| 旧设置里的档名在新包里不存在 | 回落默认档并写日志，**绝不拒绝启动** |
| 没有 `models`（**常态**） | 不出现下拉，只显示 `形态 · 体积` 一行事实 |
| 默认档 | 仍是顶层 `model`；`models` 只是额外选项 |
| **档与档之间不共享文件** | 每一档都必须是一套完整的四件套。想让两档共用同一份，只能让它们**指向同一个目录**（此时零额外体积）；只要目录不同，`conv_frontend`、`tokenizer/`、以及藏在 `decoder.onnx` 里的 embedding 都会各存一份 |

> ⚠️ **多带一档的体积代价容易被低估。** 以 Qwen3-ASR-0.6B 为例：int8 整包解压 941 MB。
> 再加一份 int4 要多 **674 MB**（不是直觉上的「砍一半」）——因为官方 int8 包里的 embedding
> 是 fp16 的 311 MB、没被量化，而 `MatMulNBits` 只管 `MatMul`/`Gemm`，词表 `Gather` 动不了。
> 完整算式见交接文档 §13。这份代价换来的是 CUDA 上仅追平 CPU、偏差 20% ⇒ 已判定不做。

> 另：档不是越多越好。int8 在 CPU / CUDA 上都有对应核，多一档换不来速度；
> 真要加速，走的是**分段指定设备**（`qwen3_ort.py`：conv 上 GPU、int8 两段留 CPU），
> 不是换量化。见交接文档 §20。

### 2.5 `python`：运行时装在哪，本体一概不管

**3FP 本体不带任何推理 DLL。** 原生库全在 python 环境里，跟着 `python` 字段走。
它决定这个包**能跑哪些档**：

| python 里装了什么 | 能跑的档 |
| --- | --- |
| `sherpa-onnx`（默认轮子，纯 CPU） | `cpu` |
| `sherpa-onnx==x.y.z+cuda` | `cuda` |
| `openvino` + 同目录 `qwen3_ort.py` | `ov-npu` / `ov-gpu` / `ov-cpu`，以及 `gpu` 档的第二选择 |
| 只有 `numpy` | llama 引擎的 `gpu` / `cpu`（GGUF 包的形态） |

选了一个 python 里没有的档，worker 在 `_capability` 或 `load()` 阶段就会跳过它，
按 §4 落到下一档并**把原因写进 notes** —— 不会崩，也不会假装在跑。

⚠️ **三个实测踩过的坑**（都是"能起来、第一次转写才崩"的类型）：

1. **`numpy` 必须带 `numpy.libs/`**。numpy 2.x 的 wheel 把 OpenBLAS 放在
   `site-packages/numpy.libs/`（与 `numpy/` 平级），只拷 `numpy/` 会在
   `import numpy` 时报 `DLL load failed while importing _multiarray_umath`。
2. **`sherpa-onnx` 顶层不拉 numpy**，所以 `import sherpa_onnx` 成功不代表 numpy 在。
3. **venv 不能真正跨机器搬**（`Scripts/*.exe` 是转发器，`pyvenv.cfg` 指向创建它的
   base python）。要彻底免依赖，就把解释器的骨架拷进包：
   `python.exe` + `python3.dll` + `python312.dll` + `vcruntime140*.dll` + `DLLs/` +
   `Lib/`（剔除 `site-packages`、`test`、`idlelib`、`tkinter`、`ensurepip`）+ 需要的
   `Lib/site-packages/<包>`。这样 `sys.prefix` 指向包内，拷到任何机器都能起。
   两个发行包就是这么做的（GPU 包 92 MB，NPU 包 477 MB，差在 openvino 248 MB）。

### 2.6 金样本自检：把"能不能装"和"算得对不对"分开

ONNX Runtime 的 `GetEpDevices()` 只回答"这个 EP 能不能装上来"，**不回答"算得对不对"**。
算子没核会**静默回落 CPU**、量化不支持会**静默算错**——两种都不抛异常，靠异常或
设备列表都判断不出来。

所以模型目录里可以放一对样本：

| 文件 | 内容 |
| --- | --- |
| `selftest.wav` | 2–5 秒 16-bit PCM wav，**单/双声道都行**（内部下混为单声道并重采样到 16 kHz） |
| `selftest.txt` | 该段音频的正确文本，UTF-8（带 BOM 也行） |

规则：

| 情况 | 行为 |
| --- | --- |
| 两个文件都在 | 切到某档后跑一次，按**字符级编辑距离 / max(1, 期望长度) ≤ 0.30** 判定（比较前剔除标点、空白、控制符并转小写） |
| 通过 | 采用该档 |
| 不通过 | 记日志、降下一档；`auto` 下就是 cuda → cpu |
| 缺任一个 / wav 读不出来 | **跳过自检**并按通过处理，日志里写明"未提供自检样本" |

有了这一层，判断"这台机器上到底能不能用这档"只需要一条通用校验，而不是为每种
设备/量化组合各写一条推断分支。

---

### 2.7 `runtime`：llama 引擎的原生运行时

只有 llama 引擎需要它。目录里必须有 `llama-server.exe` 和至少一个 `ggml-*.dll`，
缺任一项整包判无效（`llama` 引擎没有运行时就是一个跑不起来的模型目录）。

标准布局是 `runtime/vulkan/`。选它的三条理由都是实测：

| 运行时 | 体积 | 覆盖 | 相对吞吐 | 冷启 |
| --- | --- | --- | --- | --- |
| `vulkan` | **87 MB** | NVIDIA / AMD / Intel 三家 | 100% | 5 s |
| `cuda`（预构建） | 1143 MB | 仅 NVIDIA | +4.6% | 135 s |

`ggml-vulkan.dll` 的导入表里只有系统的 `vulkan-1.dll`（Windows 自带、显卡驱动提供 ICD），
**运行时在 OS/驱动层，不需要随包分发**；而 CUDA 的 `cublasLt` / `cublas` / `cudart`
是应用层库，1.1 GB 里有 452 MB 是 NVIDIA 闭源的 GEMM kernel，裁不掉。
同卡 5080 交替 A/B 只差 4.6%，冷启却差 24 倍 —— 所以基础包只带 Vulkan，
CUDA 要做也是单独一份 `runtime/cuda`（清单改一行即可切换）。

多个运行时目录可以并排放，`runtime` 指向哪个就用哪个：

```
runtime/
  vulkan/    llama-server.exe + ggml-vulkan.dll + ggml-cpu-*.dll ...
  cuda/      llama-server.exe + ggml-cuda.dll + cublas*.dll + cudart*.dll
```

`ggml-cpu-*.dll` 是按微架构分别编译的 17 个分支（sandybridge → sapphirerapids、
zen4、piledriver），运行时自己挑，所以一台机器只用到其中一个文件，但**17 个都要带**
—— 少一个就可能让另一台机器起不来。

> CUDA 那份实测还有两个前提：预构建的 `ggml-cuda.dll` 只编了
> `sm_50/61/70/75/80/90`，没有 Ada(89) 与 Blackwell(120)，新卡要靠 PTX 现场 JIT
> （这就是 135 s 冷启的来源）；而且它静态导入 `cudart64_12` / `cublas64_12`，
> 不能拿 CUDA 13 的运行库改名冒充（`cudart64_13` 缺 `cudaGetDeviceProperties_v2`，
> 即使补转发 shim，实测在 audio 初始化阶段 0xC0000005）。

---

## 3. 校验与失效

载入时按序检查，任何一步不过就返回**一句人话错误**，设置页状态行变红显示它：

| 检查 | 错误文案要点 |
| --- | --- |
| 目录为空/不存在 | 包目录不存在 |
| 目录里没有 `ai-subtitles.json` | 这个目录里没有 ai-subtitles.json |
| JSON 解析失败 | ai-subtitles.json 解析失败：… |
| `version` 不是 2 也不是 3 | 清单 version 必须是 2 或 3（读到 N）；读到 1 时另给「这是 llama.cpp 时代（version 1）的清单，格式已作废」 |
| `worker` 缺失或文件不存在 | 清单里没有写 worker / worker 脚本不存在：… |
| 模型目录不存在 | 模型目录不存在：… |
| **llama** 缺主权重 | 模型目录不合格：缺主权重 *.gguf（…） |
| **llama** 缺 mmproj | 模型目录不合格：缺 mmproj*.gguf（音频编码器，缺了模型不认音频）（…） |
| **llama** 找不到运行时目录 | 这是 GGUF 包，但找不到 llama 运行时目录（清单里的 runtime，或标准的 runtime/vulkan/） |
| **llama** 运行时里没有 `llama-server.exe` | 运行时目录里没有 llama-server.exe：… |
| **onnx** 模型目录缺件 | 模型目录不合格：缺 decoder.onnx（也接受 decoder.int8.onnx 这类带后缀的）：… / 模型目录里缺 tokenizer/ 子目录：… |
| `window`/`step` 超出 0.5–30 | 压到范围内（不报错） |

**失效行为**：功能自动停用，播放完全不受影响。修好后重新选一次目录即可（设置页状态行
实时校验，选中即生效）。包目录被删、模型被挪走也是同一条路径。

---

## 4. 推理档：一个设置项，一处降档

推理档由用户的设置项 `Ai字幕运行设备` 决定，清单不再参与。宿主把它原样传给
worker 的 `--device`，worker 先按引擎归一（`_normalize_request`），再按
`_CANDIDATES[引擎]` 的表逐个试：

**llama 引擎（默认包）**

| 用户选 | 候选顺序 | 现状 |
| --- | --- | --- |
| 自动（`auto`） | `gpu` → `cpu` | ✅ 实测 `Vulkan0 NVIDIA GeForce RTX 5080`，rtf 0.102 |
| 显卡（`gpu`） | `gpu` → `cpu` | ✅ |
| CPU（`cpu`） | `cpu` | ✅ rtf 0.372 |
| NPU（`ov-npu`） | `ov-npu` → `cpu` | 报「这个包是 GGUF 引擎，没有 NPU 路径；NPU 要装独立的 NPU 包」后降 cpu（GGUF 是 block-wise weight-only，进不了 NPU 执行单元） |

**onnx 引擎（NPU 包 / 老包）**

| 用户选 | 候选顺序 | 现状 |
| --- | --- | --- |
| 自动（`auto`） | `cuda` → `cpu` | ✅ |
| 显卡（`gpu`） | `cuda` → `ov-gpu` → `cpu` | ✅ 按这台机器实际有的显卡依次试：NVIDIA 走 CUDA EP，Intel 走 OpenVINO |
| CPU（`cpu`） | `cpu` | ✅ |
| NPU（`ov-npu`） | `ov-npu` → `cpu` | NPU 包 ✅（编译链实测通过，见 §4.3）；老包报「没有 openvino」后降 cpu |
| OpenVINO GPU（`ov-gpu`） | `ov-gpu` → `cpu` | ✅ 实测 conv 段相对偏差 0.004（上限 0.02），CER 0.03 |
| OpenVINO CPU（`ov-cpu`） | `ov-cpu` | ⚠️ 能跑但慢约 5 倍（rtf 7.0），**只用于排障** |

`npu` 是 `ov-npu` 的别名（旧设置里存过裸值）。**档名必须与宿主
`Ai字幕包.后端全部`、`qwen3_ort.BACKENDS` 三处一致** —— 这条曾经断过：宿主传
`ov-npu`、worker 只认 `npu`，选中就直接落 `auto`，连"降档"都不报。

**降档只有一处**：`load()` 抛异常或自检不通过就换下一个候选。没有第二层重试，
也没有"某档不可用时跳过某步"的补丁表——这是 v1 里 5 条回落告警 + 退出码 6 换来的简化。

⚠️ 请求的档没拿到时 worker 会发一条 `warning`（`请求 gpu，实际用 cpu：原因`），
`auto` 不报——它本来就是"有什么用什么"。**这一条是判断"到底跑在哪"的唯一可靠来源**：
别信用户的选择，也别信日志里的"已启用"，只信 `ready` 行里的 `device=`。

`selftest` 那行还有 `notes`，逐条列出每个候选为什么被跳过（"ov-npu 不可用（本机没有
NPU 设备（OpenVINO 只看到 CPU、GPU.0…））"）。排障先看它。

### 4.1 为什么 onnx 侧要自己管 session

`sherpa-onnx` 的 `OfflineRecognizer` **一个实例只吃一个 provider**，做不到
"conv 上加速器、enc/dec 留 CPU"；它随包的 `onnxruntime.dll` 还是 17 MB 的纯 CPU 构建。
所以要按段指定设备，只能自己开 session —— 这就是 `qwen3_ort.py`（`OvBackend` 用它）。

### 4.2 int8 两段为什么必须留 CPU

`encoder.int8` / `decoder.int8` 是 **QOperator 动态量化**
（`DynamicQuantizeLinear` + `MatMulInteger`），不是 GPU/NPU 友好的 weight-only 形态：

| 设备 | 后果 |
| --- | --- |
| CUDA | 插 480 个 Memcpy，encoder 698 ms（CPU 59 ms） |
| OpenVINO GPU | **输出 NaN 且不抛异常** |
| OpenVINO CPU | 能算对，但比 onnxruntime CPU 慢 5 倍 |

只有纯 fp32 的 `conv_frontend` 在哪都算得对且更快，所以段分配固定为
"conv 上加速器，int8 两段留 CPU"。这也是 `qwen3_ort.Engine.VERIFY_LIMITS` 把
conv 卡在 0.02、int8 段放宽到 0.20 的原因。

### 4.3 NPU 是怎么真正跑起来的

NPU 编译器只吃**有上界**的形状，而 `conv_frontend.onnx` 的帧维是 `[?, ?, 128]`
（上界 INT64_MAX），直接编译报的是 `Upper bounds are not specified for node 'ShapeOf'`
—— 看着像算子不支持，其实是 shape 没上界。

解法是编译前 reshape 成 `[1, 1..3200, 128]`（32 s 上界，宿主窗口上限 30 s）：

```python
model.reshape({0: ov.PartialShape([(1, 1), (1, 3200), (128, 128)])})
```

上界缺省取 3200（`qwen3_ort.NPU_DEFAULT_FRAMES`，worker 显式传同一个值）；音频更长时用
`--npu-frames` 调大。**超过上界是 OpenVINO 报错，不是崩进程** —— 这也是给上界的意义：
把"LLVM ERROR + 退出码 127"换成一次能被 Python 接住的普通异常。

**这是内存里的 shape 元数据，onnx 文件一个字节没动。** 实测（离线 `NPU_PLATFORM=NPU4000`）：

| 形状 | 结果 |
| --- | --- |
| `[1, ?, 128]` 原样 | ✗ `Upper bounds are not specified` |
| `[1, 500, 128]` 静态 | ✅ → `[1, 65, 896]` |
| `[1, 3000, 128]` 静态 | ✅ → `[1, 390, 896]` |
| `[1, 1..3000, 128]` 带界动态 | ✅ |

首次编译约 20 s（96 MB 的 vcl 编译器），结果写进 `runtime/npu-cache/`
（OpenVINO `CACHE_DIR`），第二次启动直接命中。

**怎么在没有 NPU 的机器上验证这条链**：`npu_precheck.py`。它用 `NPU_PLATFORM` 做
离线编译，所以"包里编译器是否完整"和"这台机器有没有 NPU"可以分开回答：

```
runtime\python\python.exe npu_precheck.py --model-dir model
```

发包里的 `自检-NPU.bat` 就是它 + 金样本自检两步。

**真机执行过的证据**（第三方，2026-10-29，Ultra 9 285H + Arc 140T + AI Boost，
OpenVINO 2026.4.1；原文见 `docs/ai-subtitles/NPU-第三方实测-2026-10-29.md`）：

| 设备 | 100 帧 | 500 帧 | 动态形状 | conv 相对误差 |
| --- | --- | --- | --- | --- |
| CPU（Ultra 9 285H） | 6.0 ms | 20.5 ms | 原生 | 基准 |
| NPU（AI Boost） | 3.9 ms | 11.1 ms | **带上界即可** | 0.00104 |
| iGPU（Arc 140T） | 1.1 ms | 3.4 ms | 原生 | 0.00022 |

两条必须讲清，否则照着错的结论改代码：

1. **不要做「预定义帧数集合 + 形状路由 + padding/trimming」。** 第三方把失败读成
   "NPU 不支持动态形状"，但报错是 `Upper bounds are not specified` —— 拒的是上界，
   不是"维度会变"。带上界后一次编译覆盖 1..3200 帧，不需要为每种长度各编一份；
   而 trimming 会真丢音频、padding 到离散档会改 mel 内容。
2. **1.5~1.9× 是 conv 单段的加速比，不是端到端的。** conv 每窗只跑一次前向
   （毫秒级），端到端是秒级（`OvBackend` pad30=False 实测 rtf 0.96），
   **conv 只占链路约 1%**。所以 NPU 这条腿是「能用、省电」，不是「更快」。

首次编译有两个口径：离线 `NPU_PLATFORM` 冷编译约 20 s（含 96 MB 的 vcl 编译器
初始化），真机在线编译第三方报 0.6~1.2 s。别在文档里写死一个数，看日志里的
`compile_s`。命中 `runtime/npu-cache/` 之后两处都不再付这笔钱。

---

## 5. worker 协议（包作者需要知道的）

本体 → worker 走 stdin，worker → 本体走 stdout，都是二进制帧 + JSON 行：

- **入**（每块）：`[u32 LE metaLen][meta JSON][PCM]`
  meta 含 `generation / position100ns / frames / sampleRate / channels / positions /
  sampleFormat / bytes`（`positions` 是每个槽位的 FFmpeg AVChannel 值，下混用它挑对白声道）。
  `sampleFormat == 8` 表示 `AV_SAMPLE_FMT_FLTP`（平面的 float32，按 `reshape(channels,-1).T`
  还原）；其余按 16-bit PCM 处理。
- **出**（每行一个 JSON）：

| type | 字段 |
| --- | --- |
| `status` | `state` = `loading` / `ready` / `warning` / `error`，`detail` 是人话，`fatal` 标记是否致命 |
| `selftest` | `ok` / `device` / `provider` / `detail` / `notes`（仅 `--selftest` 模式） |
| `final` | `generation` / `start100ns` / `end100ns` / `text` / `language` / `target` / `matched` |
| `stats` | `rtf` / `windows` / `dropped`（每 30 秒一条，退出前再一条累计值） |

`generation` 变化即 seek/切轨，worker 应丢弃在途窗口。

⚠️ **语言闸门随引擎强弱不同**，别一概而论：

* **llama（GGUF 包）**：输出恒是 `language X<asr_text>正文`，`X` 就是模型自报的语言，
  剥模板时顺手取出来 ⇒ `final.language` 基本都有值，`matched` 有意义（实测粤语回
  `cantonese`）。设置页那句「仅所选语言」在这条腿上**真能拦**。
* **onnx（sherpa 包）**：`result` 只有 `text`，**没有独立 language 字段**，只能从正文残留
  的前缀里抠，多数时候是空串 ⇒ `matched` 恒真，那句话**实际不生效**。

两种情况下 worker 都只如实上报 `matched`，**丢弃策略一律留在宿主侧**
（见 `LANGUAGE_SUPPORT.md`）。`ready` 行会打 `lang-gate=on` / `lang-gate=off` 标明当前是哪一种。

worker 由本体托管：BelowNormal 优先级、无窗口、stdin 关闭后 2 秒未退出即 Kill 整棵进程树。
**worker 崩溃只关字幕，播放不受影响**（P5 已实测）。

### 5.1 worker 的命令行

| 参数 | 默认 | 含义 |
| --- | --- | --- |
| `--model-dir` | **必填** | 模型目录 |
| `--engine` | `auto` | `auto` / `llama` / `onnx`；`auto` 按目录里有没有 `*.gguf` 判。**写错不判死**：与文件事实不符时按文件走并记日志（清单字段只是备注） |
| `--runtime-dir` | 空 | llama 引擎的原生运行时目录（含 `llama-server.exe` 与 17 个 `ggml-cpu-*.dll`）。空则猜 `模型目录/../runtime/vulkan`；llama 引擎下解析不出来直接 fatal |
| `--device` | `auto` | 按引擎取值：<br>llama — `auto` / `gpu` / `cpu` / `ov-npu`<br>onnx — `auto` / `gpu` / `cuda` / `cpu` / `ov-npu` / `ov-gpu` / `ov-cpu`<br>未知值按 `auto` 并记日志。`npu` 是 `ov-npu` 的别名；onnx 上 `cuda` 在 llama 上归一为 `gpu`（`_normalize_request` 做这一步，别指望后端自己认）。**onnx 的 `gpu` 不是 `cuda` 的别名**：它按 `cuda → ov-gpu → cpu` 依次试，同一台机器上 NVIDIA 走 CUDA、Intel 走 OpenVINO |
| `--window` | 5.0 | 推理窗口秒 |
| `--step` | 5.0 | 推进步长秒 |
| `--language` | `auto` | 只作**判定目标**，不传给模型（两个引擎都没有语言输入口）。闸门强度按引擎不同，见下 |
| `--threads` | 0 | 0 = `min(6, cpu_count)` |
| `--selftest` | 关 | 只加载 + 自检 + 打一行 `selftest`，然后退出（排障用） |

> **语言闸门的真实强度按后端分**：llama 与 OpenVINO（`ov-*`）的输出恒带
> `language X<asr_text>` 模板，`X` 是模型自报的语言 ⇒ `matched` 有意义
> （实测两者都回 `cantonese`）。sherpa 的 `result` 只有 `text`，没有独立 language
> 字段，只能从正文残留前缀里抠 ⇒ 多数为空、`matched` 恒真，闸门**实际不拦**。
> `ready` 行会如实打 `lang-gate=on` / `lang-gate=off`，别看错。
>
> 这条曾经按"引擎"判：引擎是 onnx 就一律打 `off`，于是 NPU 包真能判语言却自报
> 不判 —— 和"NPU 位置留桩"是同一类错误：拿静态分类代替实际后端。

排障时最省事的组合：

```
python asr_worker.py --model-dir <目录> --engine llama --device gpu --selftest
```

一行 JSON 就能看到"最终跑在哪个设备、为什么没跑在首选设备、自检结论"。

---

## 6. 设置页的两个覆盖项

| 设置项 | 默认 | 含义 |
| --- | --- | --- |
| 窗口秒覆盖 | 0（跟随清单） | 非 0 时强行覆盖 `window`，用于调参 |
| 步长秒覆盖 | 0（跟随清单） | 非 0 时强行覆盖 `step` |

覆盖项存在是为了**不为了试参数去改包**；日常使用应保持 0。

---

## 7. 迁移对照

### 7.1 v2 → v3（当前）

| v2 | v3 处置 | 为什么 |
| --- | --- | --- |
| `version: 2` | 改成 `3`（不改也能用） | v3 只是多认 `engine` / `runtime` 两个字段 |
| 模型目录三个 `.onnx` + `tokenizer/` | 换成主 `*.gguf` + `mmproj*.gguf` | 同一份权重的 int8 另一种封装；换容器后 CPU 快 2.2–2.6 倍、GPU 才真的能用 |
| —（无此字段） | 新增 `runtime: runtime/vulkan` | GGUF 需要 llama-server；Vulkan 一份覆盖三家 GPU |
| —（无此字段） | 新增 `engine: llama`（可选备注） | 让人一眼看出包是什么容器，但不参与判定 |
| `python` 必须装 sherpa-onnx | llama 包只要 numpy | 推理在原生进程里，python 只负责切窗与协议 |

**NPU 是独立的包**（1.4 GB，ONNX 三件套 + OpenVINO + 自带 python），不进主包：
GGUF 是 block-wise weight-only，进不了 NPU 的执行单元，只有 ONNX 容器 + OpenVINO
能到。主包选 NPU 时会明确报"这个包是 GGUF 引擎，NPU 要装独立的 NPU 包"并在
`notes` 里写清，然后落 CPU。

NPU 包里的实际分工是「fp32 的 `conv_frontend` 上 NPU，int8 两段留 CPU」——
原因见 §4.2，做法见 §4.3。这一条已经从"占位"变成"能编译、能跑"，区别是
`NpuBackend`（只会报错）换成了 `OvBackend`（真推理）。

### 7.2 v1 → v2

| v1 字段 | v2 处置 | 为什么 |
| --- | --- | --- |
| `version: 1` | 改成 `2` | 载入器按版本拒收 v1 |
| `model: xxx.gguf` | 改成**目录** | 模型从单文件 GGUF 变成三 onnx + tokenizer/ |
| `mmproj` | 删除 | 音频前端已经编进 `conv_frontend.onnx`，不再是单独的投影文件 |
| `llamaServer` | 删除 | 不再有子进程二进制 |
| `servers` | 删除 | 后端不再是"一个后端一份二进制"，而是 EP 名 |
| `device` | 删除 | 交给用户的设置项，包里不该预判 |
| `layers` | 删除 | llama.cpp 的 `-ngl` 概念，v2 没有对应物 |
| `llamaArgs` | 删除 | 没有 llama-server 可传参；逃生门随实现一起消失 |
| `models: {档: {model, mmproj}}` | 改成 `{档: 目录}` | 见 §2.4 |
| `python` | 语义不变，但要求变了 | 必须已装 sherpa-onnx |
| `window` / `step` / `name` / `worker` | 不变 | — |

设置页与内部设置项同步收敛：v1 的 `Ai字幕设备后端` / `Ai字幕设备序号` /
`Ai字幕扫描设备` / `Ai字幕Gpu层数` / `Ai字幕附加参数` 五项合并为一项
`Ai字幕运行设备`，旧值一律规整为 `auto`（不需要迁移代码）。
