# AI 字幕扩展包（清单 v2）

3FP 本体不内置任何 ASR 能力。要用 AI 实时字幕，就做一个**目录**，里面放一份
`ai-subtitles.json`，然后在 3FP 的设置 →「AI 字幕」页选中这个目录即可启用。

完整契约见 `docs/ai-subtitles/PACK_FORMAT.md`，这里只列上手要用的部分。

## 最小结构

```
我的AI字幕包\
  ai-subtitles.json          ← 必须有，包的自描述清单
  asr_worker.py              ← worker 脚本（路径由清单的 worker 字段决定，可以放别处）
  model\
    conv_frontend.onnx       ← 模型目录：三 onnx + tokenizer/，只钉主干名
    encoder.onnx             ← 也可以是 encoder.int8.onnx / encoder.int4.onnx
    decoder.onnx             ← 同理，量化后缀随便写，一个目录只留一份
    tokenizer\               ← 目录，内含 vocab.json / merges.txt / tokenizer_config.json
```

模型与 python 不必放进包里：清单可以指向机器上的任意绝对路径。
想做成便携包就把它们拷进包目录，然后清单里写相对路径。

> **python 必须已装 sherpa-onnx**（`pip install sherpa-onnx`）——原生库与 ONNX Runtime
> 都随那个 pip 包走，3FP 本体不带任何推理 DLL。

## 清单字段

| 字段 | 必填 | 含义 |
| --- | --- | --- |
| `version` | **是** | 必须是 `2`（1 是 llama.cpp 时代，已作废） |
| `name` | 否 | 包显示名（设置页状态行用）；缺省取目录名 |
| `worker` | **是** | worker 脚本路径。相对路径相对包根，绝对路径原样 |
| `python` | 否 | python 可执行；留空 = PATH 上的 python |
| `model` | 否 | **模型目录**；留空 = 包目录下的 `model/` |
| `models` | 否 | 档名 → 目录。**一般不要写** —— 本项目只有一份 0.6B int8 权重 |
| `window` | 否 | 推理窗口秒（0.5~30，默认 5） |
| `step` | 否 | 推进步长秒（0.5~30，默认 5；大于窗口时会被压到窗口） |

## 关于多档（`models`）

**别写。** 本项目只有一份权重：Qwen3-ASR 0.6B · int8 · 941 MB。
设置页里模型那一格显示的是这一档的事实（`int8 · 941 MB`），不是下拉 ——
定了的事不摆成选项，少一个「点了发现没别的」的坑。

`models` 只在「同一个包真并排放了两套完整权重」时才有意义，格式：

```json
"models": {
  "int8": "model",
  "另一档": "model-另一档"
}
```

任一路径不存在、缺文件，或档名写的量化形态（int8 / int4 / fp16 / fp32）与
目录里实际的 encoder 文件名不符，该档会**整档丢弃**，不会让整个包不可用。

> 换量化不是加速手段。int8 在 CPU / CUDA 上都有对应核，换 int4 反而更慢且偏差 20%。
> 要加速请看交接文档 §20 的「分段指定设备」。

## 自检样本（可选，建议加）

在模型目录里放一对文件，切档时会跑一次金样本自检——它能挡住"设备装得上但算错"的情况
（算子没核会静默回落、量化不支持会静默算错，两者都不抛异常）：

| 文件 | 内容 |
| --- | --- |
| `selftest.wav` | 2–5 秒 16-bit PCM wav，单双声道都行 |
| `selftest.txt` | 该段音频的正确文本（UTF-8） |

判定：字符级编辑距离 / 期望长度 ≤ 0.30 为通过；不通过就降下一档。缺文件则跳过自检。

## 推理档

由用户在设置里选（`自动` / `NVIDIA 显卡` / `CPU`），包不再推荐后端：

| 选 | 实际跑 |
| --- | --- |
| 自动 | 有 CUDA 就用，否则 CPU |
| NVIDIA 显卡 | CUDA；装的是纯 CPU 版 sherpa-onnx 时自动降回 CPU |
| CPU | CPU |

## 失效行为

包目录被删、清单坏了、模型不见了 → 设置页状态行变红并说明原因，功能自动停用，
**不影响正常播放**。修好后重新选一次即可。
