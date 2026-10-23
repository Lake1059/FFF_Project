# 3FP 实时 AI 字幕：使用说明（P6）

> 日期：2026-10-23 ｜ 分支 `feature/realtime-ai-subtitles-qwen3-asr`
> 面向"想在自己机器上跑起来"的人。设计与验收细节见 `MASTER_PLAN.md` 与 `P5_E2E_RESULTS.md`，
> 包格式（包作者看这份）见 `PACK_FORMAT.md`。

---

## 0. 一句话

3FP 本体**不带**任何 ASR 能力。做一个目录、里面放一份 `ai-subtitles.json`（指向 worker、
python、llama-server、模型），在 3FP 的 **设置 → AI 字幕** 里选中这个目录，播放带音轨的
媒体时字幕就会出现在画面底部（外部 SRT 之上再上一行）。

---

## 1. 准备物料（一次性）

### 1.1 模型（GGUF）

本机用的是 Qwen3-ASR-0.6B 的 llama.cpp GGUF：

| 文件 | 大小 | SHA-256 |
| --- | --- | --- |
| `Qwen3-ASR-0.6B-Q8_0.gguf` | 804,749,248 B | `bca259818b50ca7c4c05e9bdb35a5dc04fa039653a6d6f3f0f331f96f6aa1971` |
| `mmproj-Qwen3-ASR-0.6B-Q8_0.gguf` | 214,392,480 B | `41a342b5e4c514e968cb756de6cd1b7be39eff43c44c57a2ef5fc6522e36603d` |

来源：`https://www.modelscope.cn/ggml-org/Qwen3-ASR-0.6B-GGUF.git`（提交 `86222636`）。
想省显存可自己量化 Q6_K（省 349 MiB，识别质量实测与 Q8_0 一致）：

```
llama-quantize Qwen3-ASR-0.6B-Q8_0.gguf Qwen3-ASR-0.6B-Q6_K.gguf Q6_K
```

### 1.2 llama.cpp（推理后端）

解压一份带 CUDA 的 llama.cpp 发行包，记下 `llama-server.exe` 路径。
**CUDA 运行时的坑**：若 `llama-server` 起不来并抱怨 cudart，把
`torch/lib/{cudart64_13,cublas64_13,cublasLt64_13}.dll` 复制到 `llama-server.exe` 同目录
（省掉 400 MB 的运行时下载）。

### 1.3 Python 环境

worker 只需要 4 个包，**不含 torch**：

```
python -m venv E:\WorkBuddy\3FP-AI-Subtitles\venv
E:\WorkBuddy\3FP-AI-Subtitles\venv\Scripts\python.exe -m pip install numpy requests soundfile
```

（`soundfile` 会带上 `soxr`。全程约 30 MB。）

---

## 2. 做一个包

```
E:\3FP-AI-Subtitles-Pack\
  ai-subtitles.json
  asr_worker.py          ← 从 tools/ai-subtitles/asr_worker.py 拷
```

`ai-subtitles.json`：

```json
{
  "version": 1,
  "name": "Qwen3-ASR-0.6B（Q8_0）",
  "worker": "asr_worker.py",
  "python": "E:/WorkBuddy/3FP-AI-Subtitles/venv/Scripts/python.exe",
  "llamaServer": "E:/WorkBuddy/3FP-AI-Subtitles/llama-b11669/cuda/llama-server.exe",
  "model": "E:/WorkBuddy/3FP-AI-Subtitles/Qwen3-ASR-0.6B-GGUF/Qwen3-ASR-0.6B-Q8_0.gguf",
  "mmproj": "E:/WorkBuddy/3FP-AI-Subtitles/Qwen3-ASR-0.6B-GGUF/mmproj-Qwen3-ASR-0.6B-Q8_0.gguf",
  "window": 5.0,
  "step": 5.0
}
```

路径规则、留空 fallback、校验错误文案全部在 `PACK_FORMAT.md`。**相对路径是相对包根目录**，
把模型和 llama-server 拷进包里再写相对路径，就是一份可以整目录拷走的便携包。

### 2.1 一键生成便携包

`tools/ai-subtitles/make_portable_pack.py` 把上面那些手工活包了：读现有清单，
把模型 / llama-server / worker 拷进目标目录，再写出**相对路径**的清单。

```bash
# 先看它会拷什么、多大（不动磁盘）
python tools/ai-subtitles/make_portable_pack.py --source "E:/.../3FP-AI-Subtitles-Pack" --target "E:/.../pack-portable" --copy --dry-run

# 真拷（模型 800 MB + llama 几百 MB，看 dry-run 的体积再决定）
python tools/ai-subtitles/make_portable_pack.py --source "E:/.../3FP-AI-Subtitles-Pack" --target "E:/.../pack-portable" --copy

# 文件已经拷好了，只要重写清单成相对路径
python tools/ai-subtitles/make_portable_pack.py --source ... --target ... --emit
```

⚠️ **venv 不是可搬的**：`python -m venv` 生成的环境里，`Scripts/*.exe` 是转发器、
`pyvenv.cfg` 指向创建它的 base python，所以 `--copy-python` 拷过去之后目标机器仍需
同一大版本的 Python。要彻底免依赖，把 **python-build-standalone / embeddable** 发行版
放进包里，清单的 `python` 指向它即可（脚本把它的目录按普通目录处理）。

**llama-server 会整目录搬**：CUDA 构建和 cublasLt / ggml-cuda 等一堆 DLL 同目录，
只拷 exe 目标机器起不来。脚本检测到 llama-server 所在目录文件数 ≥ 4 时，会把整个
目录拷进 `runtime/<原目录名>/`，清单 `llamaServer` 与 `servers` 映射都指向里面的
`llama-server.exe`；目录体积超过 2 GB 会直接拒绝（通常是指错了上层目录）。

2026-10-23 已产出三份成品（E:\WorkBuddy\3FP-AI-Subtitles\ 下）：

- `3FP-AI-Subtitles-Pack-20261023.zip`（15 KB）：清单 + worker + README，路径绑
  本机，只适合本机或愿意手改清单的人；
- `3FP-AI-Subtitles-Pack-Portable-20261023.zip`（约 2.77 GB，store 不压缩）：
  三档权重（Q8_0 / Q6_K / Q4_K_M）+ CUDA 全依赖 + venv + README；
- `3FP-AI-Subtitles-Pack-Portable-Slim-20261023.7z`（1717 MiB，7z 格式）：
  只带 Q8_0 + Q4_K_M 两档、CUDA 目录去掉 bench/rpc/cli 工具（约 10 MB），
  固实 + LZMA2 压缩。想要三档就补一个 Q6_K 权重 + 清单加一段
  （见 PACK_FORMAT.md §2.4）。**需要 7-Zip / WinRAR 解压**。

三份清单路径都全相对，目录拷到任何机器任何路径都能用。CUDA 构建自带 CPU 臂，
没有 NVIDIA 卡的机器选「推理设备 = CPU」即可跑，无需额外下载。

**压缩实测（2026-10-23，slim 目录原始 2174 MiB）**：

| 方案 | 大小 | 耗时 |
| --- | --- | --- |
| store（不压缩） | ~2250 MiB | 10 秒 |
| -mx=5 + 固实 + 1.5G 字典 + 8 线程 | 1719 MiB | 9 分钟 |
| -mx=9 极限 + 3840M 字典 + fb=273 + 固实 + 单线程 | 1718 MiB | 34 分钟 |

极限参数比中等参数只小 1.4 MB（0.08%），时间差 4 倍——**固实 + 中等字典已吃掉全部
可挤水分**。分层看：量化 gguf 是高熵数据（Q8_0 单独压只能到 96%），压缩救不了；
CUDA DLL 有 30%+ 空间（cublasLt 445→308 MB）；固实吃到的主要是 venv/DLL 与模型
之间的跨文件冗余。**结论：缩小包靠裁档位，不靠压参数**——裁 Q6_K 一档就省 594 MiB，
比任何压缩参数都多。


---

## 3. 在 3FP 里启用

1. 打开 3FP → 设置（标题栏菜单或右键）→ 左侧分类列表末尾 **AI 字幕**；
2. 「扩展包目录」填包目录（或点浏览直接选 `ai-subtitles.json`，自动取其所在目录）；
3. 状态行会实时校验并说人话：`已就绪：Qwen3-ASR-0.6B（Q8_0）`，或红色的
   `worker 文件不存在：...`；
4. 高级选项可覆盖窗口/步长/GPU 层数（默认 = 跟随清单，日常别动）。高级区默认展开，想简洁随手折回去。

### 3.1 指定用哪块 GPU 算

「推理设备」下拉：**自动（跟随扩展包）/ CPU（不用显卡）/ NVIDIA 显卡（CUDA）/
通用显卡（Vulkan）/ AMD 显卡（ROCm/HIP）/ Intel 显卡（SYCL）**；多显卡时在高级选项里
填「设备序号」（留空 = 交给 llama-server 自选）。

- 优先级：**你的选择 > 包清单的 `device` > 自动**；切换立即重启 worker，不影响播放。
- **默认就是「自动」**：本机清单写的是 cuda，所以默认落 NVIDIA 显卡 + 全部层数卸载。
  老设置文件里存过 `cpu` 的已统一迁移为 auto（行为以界面为准，不会"显示自动、跑着 CPU"）。
- **换厂商要换二进制**：一个 llama-server 里只编了它被编译时勾的后端，所以包要么自带
  对应的一份（清单 `servers`），要么你手动把 `llamaServer` 指到那份上。状态行会说明。
- **没有显卡 / 驱动太老 / 不想占显存 → 选 CPU**。不用另外下载任何东西，任何构建都自带
  CPU 后端。实测：8/8 窗全出、文本与 GPU 臂一致，rtf 0.119–0.373（比实时快 4 倍以上），
  代价是 CPU 会被吃满。

#### GPU 层数（高级选项）

「GPU 层数」控制把多少层权重卸到显存（llama-server 的 `--n-gpu-layers`）。**留空 = 全部
卸载**（默认，也是性能最好的选择）；模型一共 28 层，显存吃紧时填小数——比如填 16，
一半权重留在内存里跑，显存占用大致减半，速度会降但通常仍在实时能力内（rtf 判据：
小于 1 就是实时）。

- 只有选显卡时这一项才有意义（选 CPU 时设备是 none，界面会置灰）。
- 它是启动期参数：改完自动重启 worker，第一次加载新配置会慢一截（权重重新分派）。
- 其余 llama-server 参数（温度、上下文、缓存策略等）为什么锁死，见 `MODEL_TIERS.md` §7
  的参数盘点——那一节写明了每项"不调"的实测依据。

细节与实测数据见 `BACKEND_MATRIX.md`。

### 3.2 指定字幕语言（可选）

「字幕语言」下拉只有四项 —— **自动（让模型自判）/ 中文（Chinese）/ English /
中文 · English（中英混说）**。默认自动，行为与以前完全一样。

**为什么只有四项**：Qwen3-ASR 官方支持 30 种语言，但我们只在真机上把中文与英文的
整链路跑通过；其余语种没有逐个验证过，用户裁定"未验证的内容不出现"，所以不列在
下拉里。老设置文件或手改 json 里残留的未验证语种值（如 `ja`）会被自动归到"自动"，
不会出现"界面显示自动、实际锁着日语"的不一致。想验证某个语种能否启用，见
`LANGUAGE_SUPPORT.md` §5（验证通过后可以从 `Ai字幕语言.全部` 整表放回面板）。

**为什么要专门给"中英混说"一档**：Qwen3-ASR 每个窗只给一个语言标签。锁中文，窗里的
英文句子可能被音译成汉字；锁英文，中文会被音译成字母。混说素材两头都不对。
这一档的做法是**不给提示**（让模型按窗自判）＋**闸门同时放行中文和英文标签** ——
唯一不需要对模型行为下赌注的方案。

先看清它能做什么、不能做什么（实测证据见 `LANGUAGE_SUPPORT.md`）：

| 它做得到 | 它做不到 |
| --- | --- |
| 把解码**引导**向所选语言，边界窗（口音重、音乐盖人声）判得更稳 | **翻译**：选中文看英语片，正文可能被写成中文，但**内容是错的**（实测 15 s 三窗全幻觉） |
| 让语言标签更稳定（模型在相邻窗之间反复换语言时有用） | **硬约束**：模型确信时压不住，中文音频选 English 仍输出中文 |
| 减少"中英混说被拆成两半"的抖动 | 方言细分（四川话、港腔粤语等 22 种暂未开放） |

- 传下去的是模型认识的**完整英文名**（`Chinese`），不是 ISO 代码。实测
  `language=zh` 被 llama-server 忽略（输出标签仍是 `English`），`language=Chinese`
  生效——这层映射由宿主做了，你不用管。
- 换语言会**重启 worker**（模型重新加载，约 10 s），播放不受影响。
- 中英混说档在闸门下放行 `Chinese` 与 `English` 两种标签，其余标签（如日语）仍会被丢弃。
- 勾上 **「只显示所选语言」**（默认关）：模型自报语言与所选不符的那一窗**不上屏**。
  开了它就意味着"选中文看英语片 = 整片没字幕"——日志会记
  `语言不符已丢弃 N 条`，状态行也会标注「只显示该语言」。语言选自动时该框置灰。

启用后：打开任意**带音轨**的媒体即自动起 worker；没有音轨（图片、无声视频）直接跳过。
标题栏 **AI 字幕** 菜单里有开关与「AI 字幕设置…」直达入口。没装包时点开关会把你送到设置页。

---

### 3.3 低性能设备：换模型档（高级）

包清单如果带了多档权重，「高级选项」里会多一个 **「模型档位」** 下拉（否则置灰）：

| 档位 | 磁盘 | 载入显存 | 什么时候用 |
| --- | --- | --- | --- |
| 默认（Q8_0） | ~972 MiB | 1991 MiB | 有独显 |
| Q6_K | ~798 MiB | 1642 MiB | 显存吃紧 |
| Q4_K_M | ~660 MiB | 更低（待实测） | **纯 CPU / 老机器** |

换档会重启 worker（约 10 s）。哪一档合适只看一件事：字幕滞后有没有超过一个窗口（5 s）。
细节、WER 对照与"怎么自己量化一档"见 `MODEL_TIERS.md`。

### 3.4 为什么不列其余 28 种语言

Qwen3-ASR 官方支持 30 种语言，但我们只在真机上把**中文与英文**的整链路跑通过。
其余 28 个的名字按模型卡填的，模型若不认这个名字，表现是**标签不变、不报错** ——
也就是"选了等于没选"。既然没有逐个验证，就不该出现在下拉里让人以为选了就生效。

它们仍然完整保留在 `FFF.Player/AI字幕/Ai字幕语言.vb` 的 `全部` 整表里（与 worker
`LANGUAGE_NAMES` 逐项对比防漂移），想确认某个语种在你机器上到底吃不吃这个名字，
跑一条单点命令即可（一次推理）：

```bash
python tools/ai-subtitles/lang_matrix.py <一段该语言的音频> --only ja
```

验证通过后放回面板要改三处（有意做得不轻易）：`主项` 加该项、`基显示名` 加显示名、
面板下拉自动跟随；`规整` 与 `check_l0.py` 的 L0-1b 会同时看守两边。

---

## 4. 你会看到什么

| 现象 | 解释 |
| --- | --- |
| 开播后 ~11 秒才出第一句 | 模型加载一次性代价（≈10 s，Q8_0）。加载期间音频在 tap 环里缓冲，**不丢字**，就绪后先把积压的窗一次吐出 |
| 字幕比正在说的音晚一窗 | 5 s 窗换准确率的既定设计（D1/D5）。稳态滞后 ≈ 一次推理：**0.3–0.75 s**（rtf 0.061–0.150） |
| 与外部 SRT 同时出现 | 决策 D4：同底并存，AI 行在 SRT 上一行 |
| 中英自动判别 | 逐窗判别；也可以在设置里锁定一种语言（3.2 节） |
| 纯音频文件（无视频轨）也有字幕 | 黑屏上显示字幕（P6 新增路径，见第 6 节） |

---

## 5. 已知限制

1. **滞后是物理下限**：窗长 5 s 决定字幕覆盖 `[end-5s, end]`。想要更短滞后就得减窗长，
   代价是识别质量下降。
2. **首窗显存**：Q8_0 稳态显存增量 1991 MiB（Q6_K 1642 MiB）。显存紧张时换 Q6_K。
3. **seek / 暂停 / 切文件的清屏**：由状态机的代次守卫 + 陈旧守卫处理，尚未做完整矩阵实测。
4. **下混是对白优先的**：tap 块带每个槽位的 AVChannel 值，worker 按位置挑而不是
   盲目平均——有 `FRONT_CENTER`（5.1/7.1 影视的对白声道）就以它为主、少量左右声道
   补空间感并归一化防削波；只有立体声就 L/R 各半；布局不明才退回逐列平均。
   早先的"全声道平均"会把对白和配乐、LFE 低频混在一起，已改掉。
5. **日志**：`<程序目录>\ai-subtitles.log`，含 worker 输出与"字幕[代次 N]"上屏行、
   "字幕图层状态 呈现帧数=… 采集块=… 丢块=… 积压=…B"。
   **有上限**：涨到 2 MB 就换成 `ai-subtitles.log.1`（旧的 `.1` 被覆盖），
   所以长期挂机不会无限涨，但也只留一份历史。
   `丢块` 非零 = 取数端跟不上消费端（推理慢到 32 MiB 音频环溢出），**这是判断
   "CPU 臂是不是已经顾不过来"的唯一指标**；正常应恒为 0。
6. **字幕语言只是提示，不是翻译**：Qwen3-ASR 是转写模型。选中文看英语片，15 s 三窗
   确实全被写成中文，但**内容是错的**（第三窗是幻觉）；中文音频选 English 则仍输出中文。
   要别的语言的字幕，只能换模型（或另接翻译模型），不是改这个下拉。
7. **同时开多个播放器窗口**：各窗口的 worker 会各自挑一个空闲端口起 llama-server
   （宿主传 `--port 0`，由 worker 向系统申请），不会互相抢占端口。代价是每个窗口
   各占一份显存——两份 Q8_0 就是约 4 GB，8 GB 卡上开两路要留意。
8. **状态机有单元测试**：`tools/ai-subtitles/state-machine-test` 直接链接
   `Ai字幕状态机.vb` 本体（不是副本），覆盖代次/陈旧/未来三条守卫与 worker 退出标志。
   纯逻辑、不碰播放器，改完状态机先跑它：

   ```
   dotnet run 指定该目录的 vbproj       # 或构建后直接执行 StateMachineTest.exe
   ```

   退出码 = 失败用例数。

---

## 6. 纯音频（无视频轨）是怎么显示的

3FP 的字幕图层原本挂在视频帧节奏上——没有视频轨，`绘制扩展定时文字` 事件根本不触发，
AI 字幕就永远上不了屏。P6 补了这条路径：

- **宿主**：字幕图层呈现器新增 `无视频时也绘制` 开关（默认关）。只有 AI 字幕装了有效包才打开，
  打开后用一个铺满客户区的虚拟显示区定位文字，且**不画外部字幕**（既有行为一字不改）；
- **内核**：`PresentTimedText` 原本要求"有画面 或 有歌词图层"才肯为定时文字建交换链。
  新增字幕图层的同源 latch（`subtitleLayoutEnabled_`），让纯音频也能建链并呈现；
  交换链一旦建起来，后续每次图层更新都会呈现，清空图层也能到达屏幕。

实测（42.6 s 纯音频 m4a，8/8 窗识别）：

```
[00:55:50] 字幕图层状态 呈现帧数=3 已绘制序号=6 命令数=1 画布=1281x720
[00:55:55] 字幕图层状态 呈现帧数=4 已绘制序号=7 命令数=1 画布=1281x720
```

`命令数=1`（AI 那一行）、`已绘制序号` 递增、`呈现帧数` 递增 —— 内核确实把它画到了屏幕上。
视频场景回归同步通过（呈现帧数 122 → 223 → 314）。
