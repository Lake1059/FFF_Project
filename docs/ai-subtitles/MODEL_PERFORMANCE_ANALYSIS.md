# Qwen3-ASR-0.6B 性能开销分析与优化空间

> 日期：2026-10-22 ｜ 阶段：Stage 1 收尾 + Stage 2 输入
> 数据来源：本机实测（`results/transformers_gpu_zh.json`）+ 模型静态分析（safetensors 头 + transformers 5.20.2 源码）
> 量化专题：**`QUANTIZATION_ANALYSIS.md`**（档位全图、VRAM/速度/质量对比、门禁协议）
> 纪律：所有数字标注【实测】或【估算】；优化项标注预期收益与验证方法，未实测不写成结论

---

## 1. 结论（TL;DR）

**这个模型本身的开销不大；"开销很大"是运行时与本机环境造成的，三者都可优化。**

| 开销类型 | 实测值 | 性质 | 可优化性 |
|---|---|---|---|
| 权重显存 | 1.457 GiB bf16【实测】 | 782M 参数的正常水平 | ✅ Q8_0 GGUF → 0.95 GiB（−35%） |
| 每窗口推理延迟 | ~0.32s 固定 + ~51ms/输出 token【估算，拟合】 | eager PyTorch launch-bound，非算力bound | ✅ torch.compile（官方 2.4×）/ llama.cpp（预期 2–4×） |
| 进程内存 | RSS 峰值 1.96 GB【实测】 | torch + 权重 mmap | ✅ worker 独立进程隔离（架构已定） |
| 加载时间 | 5.43s【实测】 | 一次性 | ✅ 常驻 worker 摊销 |
| GPU 崩溃 | WDDM 超额订阅【实测现象】 | 桌面占 5.6–7.2G/8G + bf16 1.46G | ✅ 量化到 ≤1.2G 后消失 |

核心判断：**模型是按"高并发服务端吞吐"设计的（官方：并发 128 时 2000× 吞吐），单流 batch=1 是它最弱的场景，而这正是字幕场景。** 单流延迟差的根因是 PyTorch eager 的 kernel launch 开销与 WDDM 提交延迟，不是模型算力需求——理论上限（带宽bound）约 6ms/token，实测 30–50ms/token，有 5–8 倍回收空间。

---

## 2. 模型构成：精确参量账本

safetensors 头解析（611 个 tensor，不加载权重）：

| 部分 | 参数量 | bf16 体积 | 占比 | 说明 |
|---|---|---|---|---|
| `model.language_model.layers`（Qwen3 decoder 28 层） | 440,466,432 | 840.1 MiB | 56.3% | hidden 1024，GQA 8 KV heads |
| `model.audio_tower.layers`（音频 encoder 18 层） | 173,617,920 | 331.1 MiB | 22.2% | d_model 896，Whisper 式 chunked conv |
| `model.language_model.embed_tokens` | 155,582,464 | 296.8 MiB | 19.9% | vocab 151936，**与 lm_head  tied**（省 297 MiB） |
| `audio_tower.conv*` + `multi_modal_projector` | 12,832,704 | 24.4 MiB | 1.6% | conv stem + 投影 |
| **合计** | **782,426,112** | **1.457 GiB** | 100% | 与文件字节 1,564,928,088 精确吻合 |

命名说明：商品名 "0.6B" 指 decoder+embedding（596M）；含音频塔的完整模型是 **782M**。

### 每窗口计算量（5s 窗口为例，【估算】）

- mel：500 帧（16kHz/160 hop）→ conv stem stride 8 → ~65 后卷积帧
- audio token：**13 tokens/s**（实测 2s→26 tokens，公式 `chunk_len=100 帧→13 tokens`）⇒ 5s ≈ 65 tokens
- encoder FLOPs：~65 帧 × 18 层 × ~12.8 MFLOP ≈ 15 GFLOP ≈ **0.3 ms**（4060 @50 TFLOPS 有效）
- decoder prefill（~77 tokens）：2×440M×77 ≈ 68 GFLOP ≈ **1.4 ms**
- decode（~25 tokens）：每步读全权重 1.46 GiB，带宽bound 下限 ~5.9 ms/token（4060 Laptop ~256 GB/s【估算】）

** encoder + prefill 合计 <2ms，相对实测固定开销 0.32s 可忽略。** 所有可变成本都在 decode 步。

---

## 3. 实测成本模型

中文样本（4.2s 源，2s/5s/10s 为截取/重复）GPU bf16 实测：

| 用例 | 音频 | 处理耗时 | RTF | 输出字符 | GPU util 采样 |
|---|---|---|---|---|---|
| 短 | 2.0s | 0.675s | 0.338 | 7 | 78–90% |
| 中 | 5.0s | 1.136s | 0.227 | 16 | 76–84% |
| 长 | 10.0s | 4.982s | 0.498 | ~40 | 100%（全程） |

两点拟合（2s/5s）：**固定开销 F ≈ 0.32s，decode ≈ 51ms/token**。
10s 用例超线性（拟合预测 2.4s vs 实测 5.0s）——该样本是 4.2s 源重复 2.4 倍拼接，模型对重复音频出现复读式长输出（raw 文本重复 3 遍），decode 步数远超字符数直觉，**不代表真实语音**。真实 5s 语音（约 20–25 字）预估 0.32 + 25×0.051 ≈ **1.6s/窗口**（RTF ≈ 0.32）。

GPU util 全程 76–100% 但显存只动 1.5G——**GPU 忙于小 kernel 提交，不是忙于算数**（launch-bound 的直接证据）。

---

## 4. 开销归因：为什么"感觉很大"

按影响排序：

1. **eager PyTorch 的 per-token 开销（主因）**。每 decode 一步 = 28 层 × ~10 个 kernel ≈ 280 次 launch，加 HF `generate()` 的 Python 循环与 WDDM 提交延迟。理论上限 ~6ms/token，实测 30–50ms ⇒ **5–8 倍是纯运行时税**。
2. **模型定位错配**。官方为服务端批处理设计（并发 128 吞吐 2000×）；batch=1 单流既吃不满 tensor core，又完全暴露 launch 开销。我们恰好落在它的最差用例。
3. **本机 VRAM 争用（崩溃根因）**。桌面（浏览器/QQ/WorkBuddy CEF）常驻 5.6–7.2G，bf16 模型 1.46G + KV/激活 ~0.3G ⇒ 可分配 ~1.0G < 需求 1.8G ⇒ WDDM 超额订阅 ⇒ 权重加载完成后驱动级静默杀进程（两次 15 分钟测试、一次 2 分钟复现均此形态：退出码 1、无 traceback）。中文那次能成是因为当时余量恰好够。
4. **CPU 侧噪声**。处理中进程 CPU 339–464%——mel 提取（numpy）+ Python 开销，与播放器同机争核。 worker 进程隔离后可接受。
5. **加载 5.43s**。一次性成本，常驻即摊销；但它把"临时看一眼字幕"的启动体验拉差。

---

## 5. 优化清单（按本场景 ROI 排序）

| # | 优化 | 机制 | 预期收益 | 代价/风险 | 验证方法 | 状态 |
|---|---|---|---|---|---|---|
| 1 | **llama.cpp + GGUF（CUDA）** | 融合 kernel + CUDA graph + 无 Python 循环；权重 1.46→0.95 GiB（Q8_0，已下载）；本机 `llama-quantize` 可继续自降 Q6_K/Q5_K_M/Q4_K_M | decode 2–4×；VRAM −35%，崩溃根除 | 音频 encoder 走 mmproj 路径，质量需对撞 | 同两样本 CER 对撞 + RTF/VRAM 对比；档位选择见 **`QUANTIZATION_ANALYSIS.md`** | 二进制就绪，**待用户放开实测** |
| 2 | **torch.compile（HF 路线）** | 图捕获消除 launch 开销 | 官方 A100 batch4 实测 2.4×；本机 launch-bound 更重，预期 ≥2× | 首次编译数分钟；dev 版 transformers 兼容性需验 | 编译后同样本 A/B（RTF、输出一致性） | 未测 |
| 3 | **固定语言（known language）** | 自动检测时模型要*生成* `language Chinese<asr_text>` 前缀（~6 tokens）；已知语言时该前缀是 prefill，不占 decode 步 | 每窗省 ~0.2–0.4s（51ms/token × 6）+ 输出更稳 | 需从容器音轨元数据取语言；未知时回退自动检测 | 对撞 fixed vs auto 的输出与耗时 | 机制已在 raw 输出中确认，未测 |
| 4 | **max_new_tokens 封顶（~64–96）** | 防复读/失控循环烧 GPU | 故障态保护；正常态无感 | 无（EOS 正常早停） | 代码审查 | 设计项 |
| 5 | **常驻 worker 进程** | 模型加载一次（5.43s）跨会话摊销；崩溃自隔离 | 启动字幕延迟 −5.4s；ASR 崩不拖垮播放器 | 进程生命周期管理（随播放器退出） | 集成测试 | 已入架构（见 IMPLEMENTATION_ANALYSIS） |
| 6 | **GGUF 继续降档（Q6_K/Q5_K_M/Q4_K_M）或 torchao int8wo** | 官方只发 Q8_0，但本地 llama-quantize 可自产出；decode 带宽bound 下降档同时提速 | Q6_K ≈0.85G（日常默认建议）；Q4_K_M ≈0.69G（应急） | 小模型量化更敏感；CER 必须 A/B 门禁 | 详见 **`QUANTIZATION_ANALYSIS.md`** §3–§5 | 未测，备选 |
| 7 | 窗口 3s vs 5s | 无 padding 浪费（见 §6），encoder 按时长线性；窗口小 ⇒ 单窗 prefill/decode 少但窗频高 | 首字延迟 ↓；总吞吐≈不变 | 精度下降（截断短语） | 已在决策点 #1 挂起 | 维持 5s 建议 |
| 8 | ~~30s padding 修复~~ | — | — | — | — | **已证伪，见 §6** |

**不推荐的**：CPU 推理（0.6B 在 DDR 带宽下 ~15–30ms/token，与 GPU 量化后无竞争力，且与播放器争核）；vLLM（官方仅 Linux 流式路径，本机 Windows 不可用）。

---

## 6. 已证伪假设（防重复踩坑）

**假设**：feature extractor 会把短音频 pad 到 30s（`chunk_length=30`、`n_samples=480000`），导致 5s 窗口白算 6 倍 encoder。
**证伪**：实测 2s 音频 `input_features=(1,128,200)`（=2s×100 帧/s，无 30s pad）。源码路径：`feature_extraction_qwen3_asr.py:200-207` 的 `pad(max_length=self.n_samples)` 中 `padding=True`（audio_kwargs 默认）**只 pad 到 batch 内最长序列**，`max_length` 仅在 `padding="max_length"` 时生效；`n_samples` 只是截断/上限阈值。
**附带发现**：向 `apply_transcription_request` 传 `max_length=` 会被 transformers 5.20.2 拒绝（"Kwargs have to be in processor_kwargs dict"）——穿透路径不通，且既无浪费也无需穿透。
**残留小额浪费**：mel 时间轴右 pad 到 `2×n_window=100` 帧整数倍（`:219-228`），最坏情况 ~20%（如 2.5s→300 帧），不值得处理。

---

## 7. 推荐组合（RTX 4060 Laptop 8G + 桌面争用场景）

**首选：llama.cpp + Q8_0 GGUF + 常驻 `llama-server`**（或 worker 内嵌 HTTP 客户端）
- VRAM 预算：0.95 GiB 权重 + ~0.2 GiB KV/激活 ≈ **1.2 GiB**，桌面余量 ~1.0–2.4G 下可存活（bf16 的 1.8G 不能）
- 预期每窗延迟 ~0.7–0.9s（5s 窗口，对比 eager 1.6s）
- 前置门：Q8_0 音频路径 CER 对撞（两样本，人工可判）

**备选：transformers + torch.compile + bf16**
- 不动运行时依赖树，官方支持的路径；VRAM 1.8G 仍偏紧，需用户配合关应用
- 编译 warmup 数分钟（常驻进程只付一次）

**两条路线都做的免费项**：固定语言（#3）+ max_new_tokens 封顶（#4）+ worker 常驻（#5）。

**若两条路线 CER/RTF 都不达验收**：降级到 Q4_K_M（#6）；再不行才考虑换更小模型（SenseVoice 等），那属于重新选型，不在本阶段范围。

---

## 8. 与播放器集成的关联（对 IMPLEMENTATION_ANALYSIS 的补充）

1. **worker 隔离的必要性有了量化依据**：RSS 1.96G + CPU 340–460% + VRAM 1.2–1.8G，三项都不能进播放器进程。
2. **VRAM 预算进入设计约束**：AI 字幕开启条件应含"可用 VRAM ≥ 1.5G"探测（`torch.cuda.mem_get_info` / nvidia-smi 等价物），不足时提示或自动走 Q4/CPU 档——这直接回应两次静默崩溃。
3. **延迟预算重估**：5s 窗口端到端 ~0.8–1.6s（取决于路线），叠加应用侧节流，字幕滞后落在 1.2–2.7s 区间（与既有结论一致），latest-wins + staleness guard 策略不变。
4. **15 分钟连续测试（Stage 1 强制项）改随首选路线重跑**，且在 VRAM 余量 ≥2G 时进行（用户配合关桌面应用），否则又会被 WDDM 杀。

---

## 9. 待实测清单（用户放开实机后执行）

| 项 | 命令/方法 | 判据 |
|---|---|---|
| llama.cpp Q8_0 CUDA 冒烟 | `llama-server -hf ggml-org/Qwen3-ASR-0.6B-GGUF:Q8_0`（cudart/cublas 从 torch/lib 复制，免 404MB 下载，见 `QUANTIZATION_ANALYSIS.md` §6） | 加载成功、两样本转写正确、RTF/VRAM |
| Q8_0 vs bf16 CER 对撞 | 同两样本 + 一段真实影视语音 | 字符错误率差 <1%（人工可判） |
| torch.compile A/B | 编译后同样本 3 次 | RTF 中位数提升 ≥1.8×，输出一致 |
| 固定语言收益 | fixed vs auto 各 5 窗 | 每窗耗时差、输出稳定性 |
| 15 分钟连续（首选路线） | 5s 窗/5s 步，VRAM≥2G 时 | 无崩溃、积压行为、RSS/VRAM 平稳 |
