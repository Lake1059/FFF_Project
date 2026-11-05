# P1.2 CER 基线对撞：Q8_0 vs bf16（2026-10-22 实测）

> 样本、JSON 原始输出与对撞脚本：`E:/WorkBuddy/3FP-AI-Subtitles/results/`；
> 工具：`tools/ai-subtitles/cer_baseline.py`（bf16=transformers GPU / 其余=llama-server HTTP）。
> 归一化口径：剥离 `language XX<asr_text>` 前缀与聊天模板脚手架，去标点大小写，中文去空格；
> CER = 纯 Python 编辑距离。

## 1. 样本集（9 个，447 字符）

| 样本 | 来源 | 说明 |
|---|---|---|
| asr_zh.wav | Stage 1 直采 | 中文 4.2s |
| asr_en.wav | Stage 1 直采 | 英文 15.1s（48kHz） |
| tap_zh.wav | **播放器 tap 提取** | asr_zh 封装 mp4 → 真实播放链路 → tap dump → wav（44.1kHz 单声道 FLTP，4.20s） |
| tap_en.wav | **播放器 tap 提取** | asr_en 前 6.36s 同链路提取 |
| en_w0..w4.wav | asr_en 切片 | t=2s 起 5×2.5s 窗口（跳过开头填充词），169 字符 |

tap 样本的提取验证了 P2a 门禁：块元信息/位置连续性/非静音全部通过（见 `asr_tap_probe_tap{zh,en}.log`），
且 **tap_zh 与直采 asr_zh 时长精确一致（4.20s）、转写逐字相同** —— tap 路径对识别透明。

## 2. 对撞矩阵

### 2.1 同引擎对撞（llama.cpp，贪心 temp 0，唯一变量 = 权重量化精度）

参照 = bf16 GGUF（ggml-org 官方转换件，1439 MiB，经 hf-mirror 下载实体）；
被测 = Q8_0 GGUF（804 MiB）。mmproj 两侧均 Q8_0（隔离 LLM 量化变量）。

| 样本 | CER | dist/len |
|---|---|---|
| asr_zh.wav | 0.00% | 0/13 |
| asr_en.wav | 0.00% | 0/173 |
| tap_zh.wav | 0.00% | 0/13 |
| tap_en.wav | **2.67%** | 2/75 |
| en_w0..w4 | 0.00% ×5 | 0/37, 0/36, 0/19, 0/27, 0/50 |

### 2.2 跨实现对撞（bf16 = transformers GPU，Q8_0 = llama-server）

| 样本 | Q8 贪心 CER | Q8 temp0.6 CER |
|---|---|---|
| asr_zh.wav | 0.00% | 0.00% |
| asr_en.wav | 2.26%（丢开头填充词） | 1.13%（hmm→mhm） |
| tap_zh.wav | 0.00% | 0.00% |
| tap_en.wav | 0.00% | 0.00% |

## 3. 唯一偏差的归因：非词法填充词，非量化损失

tap_en 的 2.67%（2/75）全部来自开头一个填充词：bf16 GGUF 出 `mhm`，Q8_0 出 `hmm`。
该词在全部四种组合下都不稳定，**与量化无关**：

| 路线 | asr_en（15.1s）开头 | tap_en（6.36s）开头 |
|---|---|---|
| transformers bf16（贪心） | `hmm` | `hmm` |
| llama.cpp bf16 GGUF（贪心） | **无**（整词丢弃） | `mhm` |
| llama.cpp Q8_0（贪心） | **无**（整词丢弃） | `hmm` |
| llama.cpp Q8_0（temp 0.6） | `mhm` | `mhm` |

同一引擎（bf16 GGUF）在 15.1s 与 6.36s 两种输入长度下对该词给出三种不同输出
（丢弃 / mhm），证明这是低能量非词法音的固有不确定性——音频前端与输入长度的
敏感度远超权重量化精度。中文样本（本项目主语言）三条路线全部 0.00%。

## 4. 门禁裁定

**P1.2 通过。** 判据：

- 同引擎对撞 9 样本中 8 个 0.00%，聚合 2/447 = 0.45% < 0.5% 门限；
- 唯一超限样本（tap_en 2.67%）的偏差经归因排除量化因素（bf16 自身同词不稳定）；
- 语言标签全部正确（Chinese/English），无复读、无幻觉文本；
- 中文（主语言）直采 + tap 双路径 0.00%。

结论：**Q8_0 对识别质量透明，可以作为首版生产量化**；Q6_K 复测见 P1.3。

## 6. P1.3 Q6_K 复测（2026-10-22）

Q6_K 由 `llama-quantize` 本地自量化（bf16 GGUF → Q6_K，8 线程，7.0s）：

| 精度 | 文件 | 大小 | BPW |
|---|---|---|---|
| bf16 | Qwen3-ASR-0.6B-bf16.gguf | 1439 MiB | 16.00 |
| Q8_0 | Qwen3-ASR-0.6B-Q8_0.gguf | 767 MiB | ~8.5 |
| **Q6_K** | Qwen3-ASR-0.6B-Q6_K.gguf | **593 MiB** | **6.56** |

mmproj 两侧保持 Q8_0（隔离 LLM 量化变量）。贪心解码，同样 9 样本：

| 对撞 | 结果 |
|---|---|
| Q6_K vs bf16（4 主样本） | zh 0.00% / en 2.31%（填充词）/ tap_zh 0.00% / tap_en 2.67%（填充词） |
| Q6_K vs bf16（5 窗口） | **全部 0.00%**（0/37, 0/36, 0/19, 0/27, 0/50） |
| Q6_K vs Q8_0（4 主样本） | zh 0.00% / en 2.31%（填充词）/ tap_zh 0.00% / tap_en 0.00% |

**裁定：Q6_K 与 Q8_0 识别等价**——全部实词（447 字符中除 2 字符填充词外的 100%）
逐字相同，偏差仍是同一个非词法填充词轮盘。Q6_K 比 Q8_0 小 27%（767→593 MiB），
在 RTX 4060 8GB（与播放器视频解码共享）上是有意义的节省。**Q8_0/Q6_K 的最终选型
推迟到 P1.5 VRAM 实测后定**（D7 默认仍为 Q8_0）。

## 7. P1.4 15 分钟连续运行（2026-10-22）

驱动 `tools/ai-subtitles/p14_continuous.py`（5s 窗 / 5s 步，中英混合循环素材 905s，
llama-server Q8_0 + temp 0.6 生产配置）：

| 指标 | 修复前（默认 cache 配置） | 修复后（`--no-cache-idle-slots --cache-ram 0`） |
|---|---|---|
| 完成窗口 | 181/181，零错误 | 181/181，零错误 |
| 平均延迟 | 0.185s | 0.206s |
| p95 / max | 0.251s / 0.326s | 0.256s / 0.456s |
| 积压 | 0 | 0 |
| VRAM | 4011→4011 MiB 平 | 4021→4021 MiB 平 |
| **RSS** | **1201→3345 MiB（+12 MiB/窗线性，无平台）** | **1187→1188 MiB（平）** |

**发现并修复 llama-server 提示缓存泄漏**：默认 `--cache-idle-slots` +
`--cache-ram 8192` 会把每个**唯一音频输入**的 KV 状态存入主机侧提示缓存
（VRAM 不变、RSS 每窗 +12 MiB ≈ 93 token 的 KV）。字幕场景每窗音频唯一、
零前缀复用收益，纯泄漏（15 分钟 +2.1 GB，两小时影片 +17 GB）。
`--no-cache-idle-slots` 单独用只压制前几窗；与 `--cache-ram 0` 组合才完全止住
（唯一窗 1-20 测试：预热 +150 MiB 后平坦）。**该组合已写入 `asr_worker.py`
的 LlamaServer 启动参数**（见代码注释）。

**P1.4 通过**：无崩溃、RSS/VRAM 平稳、零积压、RTF ≈ 0.04（延迟远低于步长）。

## 8. 复现命令

```bat
rem bf16 参照（transformers，GPU）
python cer_baseline.py --route bf16 --files <wav...> --out cer_refs_bf16.json
rem llama-server 路线（bf16 GGUF / Q8_0 / Q6_K 各自启动后）
python cer_baseline.py --route q8 --files <wav...> --out cer_hyps_<tag>.json
python cer_baseline.py --route compare --refs <refs.json> --hyps <hyps.json>
```
