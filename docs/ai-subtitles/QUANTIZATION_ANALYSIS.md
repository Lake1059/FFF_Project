# Qwen3-ASR-0.6B 量化选择分析与建议

> 日期：2026-10-22 ｜ 场景：3FP 实时字幕 worker，RTX 4060 Laptop 8G，桌面争用 5.6–7.2G，单流 batch=1
> 依赖事实：`MODEL_PERFORMANCE_ANALYSIS.md`（成本模型：launch/带宽bound，decode ~51ms/token）
> 纪律：文件大小/存在性为【实测】；体积速度为【估算】；CER 影响全部待 A/B 门禁

---

## 1. 结论（TL;DR）

**官方只发布 Q8_0 一档，但本地 `llama-quantize` 可自产出任意 K 档——量化是本场景唯一同时解"VRAM 崩溃"和"解码慢"两个问题的杠杆。**

| 决策 | 选择 | 理由 |
|---|---|---|
| 验收基线 | **Q8_0 原样**（已下载，0.95 GiB） | 官方唯一发布档；作为 CER 参照系 |
| 日常默认 | **自量化 Q6_K**（main）+ mmproj 保 Q8_0 ≈ 0.85 GiB | VRAM/质量平衡点；0.6B 小模型不宜默认 Q4 |
| 应急档 | Q5_K_M / Q4_K_M + `--gpu-layers` 部分卸载 | 桌面占 >6.5G 时；Q4 必须过 CER 门 |
| HF 兜底 | torchao **int8wo**（≈0.8 GiB） | llama.cpp 音频路径质量不合格时启用 |
| 不投入 | AWQ/GPTQ、TensorRT、ONNX、FP8、bnb、vLLM | 见 §5，均有更优替代或硬伤 |

附带收益：decode 是带宽bound，权重每减 1G/token 的读取量直接转化为延迟——Q6_K 预期比 Q8_0 再快 ~1.2×，Q4_K_M ~1.5×（带宽理想值，实测打折）。

---

## 2. 可用量化面全图

### Route A：llama.cpp GGUF（首选）

**官方仓库实际供给**【实测：ModelScope resolve URL HEAD 探测 9 个标准命名】：

| 文件 | 状态 | 大小 |
|---|---|---|
| `Qwen3-ASR-0.6B-Q8_0.gguf` | ✅ 存在（已下载） | 804,749,248 B |
| `mmproj-Qwen3-ASR-0.6B-Q8_0.gguf` | ✅ 存在（已下载） | 214,392,480 B |
| Q4_K_M / Q5_K_M / Q6_K / IQ4_XS / f16 / mmproj-f16 | ❌ 全部 404 | — |

⇒ 官方只发 Q8_0。**但** `llama-b11669/cuda/llama-quantize.exe` 已在手（实测存在），可从 Q8_0 本地自量化，CPU 运行、0.6B 规模秒级到分钟级、零下载：

```
llama-quantize Qwen3-ASR-0.6B-Q8_0.gguf Qwen3-ASR-0.6B-Q6_K.gguf Q6_K
```

注意事项：
- **二次量化代价**：Q8_0→Q4 比 f16→Q4 多损失一点（K-quant 实践中可接受）；若要纯净 f16 源需 `convert_hf_to_gguf.py` 从 safetensors 转（脚本在 llama.cpp 源码仓，GitHub 可达性未验证）——不值得，除非 Q6_K 的 CER 门不过。
- **mmproj 只有 Q8_0，而这正好是我们要的**：音频 encoder 的量化伪影直接污染声学特征，比 decoder 文本量化更敏感，保持 Q8_0（或未来官方出 f16 再升）。主模型降档、mmproj 保 Q8 是正确的不对称策略。
- KV cache 量化（`--cache-type-k q8_0`）：本场景上下文 ~200 token，KV 仅 ~10MB，**无意义，不启用**。

### Route B：transformers + torchao（HF 路线兜底）

无需任何下载，直接从已持有的 safetensors 在加载时量化，纯 PyTorch、Windows 兼容：

| 档位 | 权重体积【估算】 | 说明 |
|---|---|---|
| bf16（现状） | 1.46 GiB【实测】 | 基线 |
| int8wo | ~0.80 GiB | 每通道 int8 权重only；torchao 最稳档 |
| float8wo | ~0.80 GiB | Ada（sm89）FP8 e4m3；与 int8 同体积但数值分布更优，收益边际 |
| int4wo | ~0.44 GiB | 分组 int4 无校准；0.6B 上风险高于 GGUF Q4_K_M（K-quant 有 per-super-block 缩放优化） |

适配前提：transformers 5.20.2 的 Qwen3ASR 自定义 attention（cu_seqlens varlen）与 torchao 的 weight-only 替换兼容性需实测——torchao 的 `quantize_` 走 nn.Linear 替换，该模型的 Linear 层应全覆盖，但 conv2d/投影层不在量化范围（本来就只占 1.6% 参数）。

### Route C：明确不推荐

| 方案 | 排除理由 |
|---|---|
| AWQ / GPTQ | 无公开量化 checkpoint（模型太新）；自校准需过音频路径构造校准集，工作量与收益倒挂 |
| TensorRT FP8/INT8 | 自定义音频 attention + varlen 的 ONNX trace 未验证；engine 版本锁；build 需 GPU 重载；实验分支不值得 |
| ONNX Runtime QDQ | 同 TRT 的导出风险；encoder 静态量化还需校准集 |
| bitsandbytes int8 | Windows CUDA wheel 兼容性历来脆；torchao int8wo 覆盖同需求且更干净 |
| vLLM FP8 | 官方流式路径仅 Linux；Windows 需 WSL2，违背独立进程轻量部署 |

---

## 3. 档位对比表（VRAM / 速度 / 质量）

桌面余量观测 0.8–2.4G（浏览器/QQ/CEF 5.6–7.2G）。CUDA context + buffer 约 0.3–0.45G 为不随量化缩小的地板。

| 档位 | 权重总量【估算】 | VRAM 总量【估算】 | 每 token 读取 | 相对 Q8_0 解码速度 | CER 预期（待门禁） |
|---|---|---|---|---|---|
| bf16（现状 eager） | 1.46 GiB | ~1.85 G ❌ 已崩 | 1.46 G | —（被 launch 税淹没） | 基准 |
| Q8_0 | 0.95 GiB | ~1.4 G ⚠️ 边际 | 1.02 G | 1.0×（且消除 launch 税后绝对快 3–5×） | ≈bf16（参照系） |
| Q6_K | ~0.85 GiB | ~1.25 G ✅ | ~0.85 G | ~1.2× | Δ ≤0.3% abs 预期无感 |
| Q5_K_M | ~0.78 GiB | ~1.2 G ✅ | ~0.75 G | ~1.3× | Δ ≤0.5% abs，小模型需验 |
| Q4_K_M | ~0.69 GiB | ~1.1 G ✅ | ~0.65 G | ~1.5× | Δ ≤1.0% abs，**必须 A/B** |
| Q3_K_M | ~0.61 GiB | ~1.0 G ⚠️ | ~0.55 G | ~1.7× | 预期可感下降，仅极端应急 |

配 `--gpu-layers <部分>`（如 decoder 20/28 层上 GPU、encoder 留 CPU）可把 VRAM 压到 ~0.6–0.7G，速度代价 ~30–50%——比 Q3 更优先使用的应急手段（质量无损）。

**关键认知：即使 Q4_K_M，VRAM 地板 ~1.1G 仍可能被 7.2G 桌面对穿。** 量化把"必崩"变成"多数时候能活"，但不能保证与满载桌面共存；开启前的 VRAM 探测 + 不足时提示用户关应用，仍是必要产品逻辑（见 IMPLEMENTATION_ANALYSIS §8）。

---

## 4. 质量门禁协议（CER A/B）

量化是**质量换资源**的唯一动作，必须门禁化：

- **样本集**（四类，覆盖 README 声明能力）：① 中文短样本（已有 `samples/asr_zh.wav`）② 英文短样本（已有）③ 真实影视对白（`testmedia/media/test.mp4` 音轨提取）④ 带 BGM/噪声（README 称支持 songs with BGM，需补素材）
- **判据**：贪心解码输出逐字符 diff；ΔCER(档位 vs Q8_0) ≤ 上表阈值；**任一样本类别超阈值即回退上一档**
- **复现纪律**：单轮噪声可达 3 倍（项目既有结论），每档 ≥2 轮
- **附加检查**：语言标签是否正确（`language Chinese/English`）、无复读崩溃（复现 10s 样本的复读行为是否被量化放大）

---

## 5. 建议（分层决策）

1. **第一步（解除测试阻塞后立即做）**：Q8_0 原样跑通 llama-server CUDA + 两样本转写 + RTF/VRAM——这是所有档位的参照系。
2. **日常默认 = Q6_K**：`llama-quantize` 本地产出（分钟级），VRAM ~1.25G 在桌面 ≤6.7G 时可存活，CER 预期无感。**不建议 0.6B 默认 Q4**——小模型对量化更敏感，字幕场景的错误是用户直视的。
3. **应急 = Q5_K_M/Q4_K_M + 部分卸载**：桌面 >6.5G 时走这条；Q4 必须带 CER 门禁数据上线。
4. **HF 兜底 = torchao int8wo**：仅当 llama.cpp 的 mmproj 音频路径 CER 不合格（比如音频特征被 mtmd 预处理改变）时启用；届时 bf16→int8wo 是质量最稳的降档。
5. **不投入**：§Route C 全部。实验分支的每一分钟都应花在集成上，不是追 5% 的推理框架收益。

---

## 6. 顺手解除的阻塞：CUDA 运行时免下载

llama-b11669 CUDA 版需要 cudart + cublas（13.x）。原计划下载的 404MB `cudart.zip` **不需要了**【实测】：torch 2.14.1+cu132 venv 自带全套：

```
C:/Users/20210/.workbuddy/binaries/python/envs/default/Lib/site-packages/torch/lib/
  cudart64_13.dll  cublas64_13.dll  cublasLt64_13.dll
```

复制这三个到 `llama-b11669/cuda/` 旁即可（驱动 616.92 已跑 torch cu132，CUDA 13.x 运行时兼容无虞；ggml-cuda.dll 若还需其他 13.x 组件再从该目录补）。

---

## 7. 待办（用户放开实机后）

| 项 | 方法 | 判据 |
|---|---|---|
| Q8_0 冒烟 | 复制 3 DLL → `llama-server -hf ...:Q8_0`（或 `-m + --mmproj`） | 加载成功、两样本转写正确、RTF/VRAM 落盘 |
| Q6_K 自量化 | `llama-quantize` 本地产出 | 文件 ~0.62G、llama.cpp 能加载 |
| CER A/B | §4 协议，Q8_0 vs Q6_K（→Q5→Q4 递进） | ΔCER 阈值 |
| VRAM 实测 | nvidia-smi 稳态读数 | 与 §3 估算对撞，修正表格 |
| torchao int8wo（条件触发） | 仅当 mmproj CER 不合格 | 同上 CER 协议 |
