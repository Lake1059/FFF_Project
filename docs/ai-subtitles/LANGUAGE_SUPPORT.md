# 字幕语言指定：能做什么、怎么做的、证据在哪

日期：2026-10-23　　内核无关（纯宿主 + worker + llama-server）

## 0. 一句话结论

**可以指定，但它是"软提示"，不是"翻译"，也不是"硬过滤"。**

- 能：把解码**引导**向所选语言，语言标签跟着走；长音频连续多窗时引导会**累积增强**
  （下文 §1.1 的整段实测里，英文语音被整段写成中文）；
- 不能：当作翻译用。写出来的中文**对不上原意**，质量不可用；
- 因此"只出这个语言"由**宿主闸门**实现（勾选「只显示所选语言」时丢弃不符的窗），
  不指望模型自己守规矩。而实测表明模型通常"顺从"，闸门主要拦反例
  （如中文音频设了 English）。

## 1. 实测证据（b11669 CUDA + Qwen3-ASR-0.6B Q8_0）

同一台机器、同一个 `llama-server`、同一批样本，只改 `language` 表单字段：

| 样本 | 请求 | 返回 text（截断） | input_tokens |
| --- | --- | --- | --- |
| `samples/asr_zh.wav` | 无字段 | `language Chinese<asr_text>甚至出现交易几乎停滞的情况。` | 80 |
| `samples/asr_zh.wav` | `language=English` | `language Chinese<asr_text>甚至出现交易几乎停滞的情况。` | 85 |
| `samples/asr_zh.wav` | `language=zh` | 同上 | 85 |
| `samples/asr_en.wav` | 无字段 | `language English<asr_text>Oh yeah, yeah. He wasn't even that big…` | 223 |
| `samples/asr_en.wav` | `language=Chinese` | `language Chinese<asr_text>嗯哼，哦， yeah, yeah. He wasn't even that big…` | 228 |
| `samples/asr_en.wav` | `language=zh` | `language English<asr_text>Hmm. Oh, yeah, yeah…` | 228 |
| `samples/asr_en.wav` | `language=Klingon` | `language English<asr_text>Mhm. Oh, yeah, yeah…` | 229 |
| `samples/asr_en.wav` | `prompt=Vocabulary: Quilter, apostle, gospel.` | `language English<asr_text>Hmm. Oh, yeah, yeah…` | 229 |

四条硬事实：

1. **字段确实进了 prompt**：`input_tokens` 从 80/223 涨到 85/228（+5），换更长的值还会再涨
   （`Klingon` 229、`prompt` 229）。所以不是"被悄悄丢弃"。
2. **ISO 代码无效**：`language=zh` 对英语音频的输出标签仍是 `English`，
   而 `language=Chinese` 变成了 `Chinese`。原因是 llama.cpp 把这个字符串**原样拼进
   prompt 文本**，模型只认训练时见过的语言名。
3. **软约束**：中文音频 + `language=English`，模型仍输出 `language Chinese` ——
   模型确信时提示压不住。所以不能靠它做"只出中文"。
4. **不是翻译**：英语音频指定 Chinese，正文仍是英文，只是被诱导出中文语气词
   （`嗯哼，哦，`）。这是伪影，不是想要的输出。

顺带确认：`prompt` 字段同样进 prompt（229），可用于热词/术语偏置
（Qwen3-ASR 官方的 context/hotwords 走 system 消息，原理一致）。**本次未做**，
留作后续：接一个"术语表"设置项即可。

### 1.1 整段（多窗）走 worker 时引导更强

单窗 curl 里英语音频只被改了标签，正文还是英文；但换成真实链路
（`p5_worker_smoke.py` 喂 15 s 英语音频、`--language zh` → 传 `Chinese`）后，
三个窗**全部写成了中文**：

```
[out] {"type":"status","state":"ready","detail":"window=5.0s step=5.0s lang=Chinese"}
[out] {"type":"final",...,"text":"哦耶！他不是一个大人物，我笑了。","language":"chinese","target":"Chinese","matched":true}
[out] {"type":"final",...,"text":"或者，是某种感觉。","language":"chinese","target":"Chinese","matched":true}
[out] {"type":"final",...,"text":"我给你发一万，我送一万，送一千。","language":"chinese","target":"Chinese","matched":true}
```

两条结论：

- **引导是真的**：ISO 映射（`zh`→`Chinese`）走通，worker 的 `ready` 行与 stderr
  `language hint: Chinese` 都印证了传参链路；
- **质量不可用**：翻译是错的。原文是 "Oh yeah… he wasn't even that big…" 之类，
  第三窗完全是幻觉。所以**不能把"选中文"当翻译功能卖**，只能当"锁定解码语言"。

`matched` 全为 `true` 也说明：模型顺从时闸门不会触发——闸门不是用来纠正
"选错语言"，而是给"模型不听话"（如中文音频选了 English）兜底。

## 2. 分层设计

| 层 | 做什么 | 在哪 |
| --- | --- | --- |
| 标识与归一 | ISO（`zh`）→ 模型英文名（`Chinese`）；30 种语言与中文显示名 | `FFF.Player/AI字幕/Ai字幕语言.vb` |
| 传输 | 设置 → `--language`；`auto` 也显式传，日志可追溯 | `Ai字幕管道.启动` → worker |
| 提示 | `POST /v1/audio/transcriptions` 时带 `language=<英文名>` | `asr_worker.transcribe()` |
| 上报 | `final` 增 `target` / `matched`，worker 从不自己丢结果 | `asr_worker` 主循环 |
| 闸门 | 「只显示所选语言」时丢弃 `matched=false` 的窗，按 30 s 限流记日志 | `Ai字幕管道.处理结果行` |
| 重启 | 语言是启动期参数，改了要重启 worker（约 10 s，播放不受影响） | `播放器AI字幕控制器.应用设置` |

为什么要 worker 上报、宿主决定丢弃：那个开关不该逼人重载 1 GB 模型。
worker 只报事实，策略留在宿主。

## 3. 语言清单

30 种，取模型卡（`Qwen3-ASR-0.6B` README）的语言表；`Ai字幕语言.名称表`
保存 ISO → 英文名的映射。中文方言（四川话、港腔粤语等 22 种）**暂不开放**：
官方说法是支持，但提示词写法没有实证，宁可先不给。

| 代码 | 传下去的值 | 代码 | 传下去的值 |
| --- | --- | --- | --- |
| zh | Chinese | en | English |
| yue | Cantonese | ja | Japanese |
| ko | Korean | fr | French |
| de | German | es | Spanish |
| ru | Russian | pt | Portuguese |
| it | Italian | ar | Arabic |
| th | Thai | vi | Vietnamese |
| id | Indonesian | ms | Malay |
| tr | Turkish | hi | Hindi |
| nl | Dutch | pl | Polish |
| sv | Swedish | da | Danish |
| fi | Finnish | cs | Czech |
| el | Greek | hu | Hungarian |
| ro | Romanian | fil | Filipino |
| fa | Persian | mk | Macedonian |

**面板只列 4 项**（自动 / 中文 / English / 中英混说，2026-10-23 起，用户裁定
"未验证的内容不出现"）。整表保留在 `Ai字幕语言.全部`，用途只有一个：与 worker
`LANGUAGE_NAMES` 逐项对比防漂移（check_l0.py L0-1）。`规整` 只认这 4 项 ——
老设置/手改 json 里残留的 `ja` 之类的值一律归 auto，不会带着行为走。

只有 `Chinese` / `English` 有直接实证；其余按官方 README 的名字，
若某个名字模型不认，表现是"标签不变"（不会报错），排查办法见 §5。

## 4. 复验方法（不需要播放器）

```bash
# 起服务（模型、mmproj 换成你自己的路径）
llama-server -m Qwen3-ASR-0.6B-Q8_0.gguf --mmproj mmproj-...gguf -ngl 99 --port 18099

# 一次请求就能看出字段是否生效：看 text 开头的 language 标签 + input_tokens
curl -s -F "file=@asr_en.wav" -F "model=qwen3-asr" -F "response_format=json" \
     -F "language=Chinese" http://127.0.0.1:18099/v1/audio/transcriptions
```

判据：`text` 以 `language Chinese<asr_text>` 开头且 `input_tokens` 比不带字段时多
≈5 → 生效；标签还是 `English` → 这个名字没被模型接受。

宿主侧看 `<程序目录>\ai-subtitles.log`：

- 启动：`AI 字幕：字幕语言 中文（Chinese）（提示，不筛除）`
- 每窗：worker stderr 的 `final [...] lang=Chinese ...`，不符时带 `(target=Chinese, mismatched)`
- 闸门触发：`AI 字幕：语言不符已丢弃 N 条（实际 English，只显示 中文（Chinese））`

## 4.5 中英混说档（`zh-en`）

Qwen3-ASR 每个窗只输出**一个**语言标签，所以单语言档在混说素材上是两难：
锁中文，窗里的英文句子可能被音译成汉字；锁英文，中文被音译成字母。

这一档的做法是**不对模型下任何赌注**：

| 面 | 行为 |
| --- | --- |
| 提示 | **不传** `language`（等价 auto），模型按窗自判 |
| 闸门 | 勾了"只显示所选语言"时，放行 `Chinese` 与 `English` 两种标签，其他语言照丢 |
| 标签缺失 | 保留（与单语言档同一原则：宁可显示，也不静默全黑） |

代价：它**不解决**单个窗内"中英混排被模型统一成一种语言"的问题 —— 那要模型侧支持，
不是宿主能改的。它能解决的是"整片被闸门杀光"与"锁定语言导致音译"这两件事。

## 5. 未做 / 风险

1. **翻译**不在本功能范围内。指定语言≠翻译：§1.1 里整段英文被写成中文，但内容是错的。
   要"外语片中文字幕"必须接一个翻译模型（另起一个 llama-server 或调外部 API），
   是独立功能，别混进语言下拉。
2. **中英混说**：Qwen3-ASR 每窗只给一个语言标签。锁定中文时，窗口里的英文句子
   可能被音译成汉字；勾了闸门则会整窗被丢。两种都不理想——这类素材建议留 auto。
3. **方言**未开放；`--language` 收到未知值时原样透传（日志能看到，模型忽略）。
   宿主的 `规整` 已经只放行 4 个主项，所以 worker 侧实际只会收到 auto/Chinese/English。
4. **闸门默认关**：开了它，"选中文看英语片"就是整片空白。默认关是按
   "宁可显示错的，也不要看起来像坏了"的取舍。
5. **只有 `Chinese` / `English` 有整链路实证**。其余 28 种不上面板（见 §3）；
   如果模型不认某个名字，表现是"标签不变"（不报错），用 §4 的 curl 判据逐个确认，
   验证通过后再从 `全部` 整表放回面板（改 `主项` + `基显示名` 两处）。
