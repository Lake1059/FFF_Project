# P5 真机端到端验收（含 P1.5 稳态显存）


> ⚠️ **路线已变更（2026-10-27）**：本文写于 llama.cpp 时代（`llama-server` 子进程 + GGUF 双文件）。
> 推理栈现已换成 **sherpa-onnx（ONNX Runtime）进程内**：后端只剩 `cpu` / `cuda` 两档 + `npu` 占位，
> 模型是一套 ONNX 目录而非 .gguf，扩展包清单升到 **v2**（见 `PACK_FORMAT.md`）。
> **已失效**：端到端延迟、显存/内存账单的具体数字
> **仍有效**：三个真机缺陷的根因（那些不随推理栈变）
> 归并理由与新方案全貌见主仓 `docs/交接文档-2026-10-27-设备选择统一方案.md`。

日期：2026-10-22　分支：`feature/realtime-ai-subtitles-qwen3-asr`（worktree `_3fp-ai-subtitles`）
内核：`FFF.Native` Release x64（含 tap 导出）　宿主：`FFF.Player` Release

结论先行：**实时 AI 字幕链路在真机上跑通**，8 个窗口全部命中、中英混合识别正确、字幕确实上屏；
worker 被强杀只关字幕、播放器存活。发现并修复 3 个真机才暴露的缺陷（详见第 4 节）。

---

## 1. 物料与环境

| 项 | 值 |
| --- | --- |
| 素材 | `tmp/e2e_45s.mp4`，42.64 s，h264 320x240@15 + aac 44.1 kHz mono |
| 素材构成 | `tap_zh.mp4`(4.2 s) / `tap_en.mp4`(10 s) 交替 ×3（concat `-c copy`） |
| 模型 | `Qwen3-ASR-0.6B-Q8_0.gguf` + `mmproj-...-Q8_0.gguf` |
| 推理后端 | `llama-b11669/cuda/llama-server.exe`，`--n-gpu-layers 999 --ctx-size 4096` |
| worker | `venv/Scripts/python.exe`（numpy / requests / soundfile / soxr，**无 torch**） |
| 派发方式 | schtasks（沙箱注入会让进程树内的 DXGI 播放路径崩溃，必须派发到进程树外） |
| 上屏证据 | 控制器在文本变化时写 `<程序目录>\ai-subtitles.log`，行首 `字幕[代次 N]` |

窗口/步长 = 5 s / 5 s（无重叠）；内核 tap 环默认 32 MiB。

---

## 2. 验收结果

### 2.1 worker 独立冒烟（不起播放器，按协议直接喂帧）

`tools/ai-subtitles/p5_worker_smoke.py` —— stdin 帧 `[u32 LE metaLen][meta JSON][PCM f32]`，
把 5 s 立体声素材连播两遍：

```
final [  0.00..  5.00] (english) Hmm. Oh yeah yeah, he wasn't even that big when I started.
final [  5.00.. 10.00] (english) Mm hmm. Oh yeah, yeah. He wasn't even that big when I started.
exit=0 fed=10.00s audio in 10.3s   final windows: 2   dropped=0
rtf = 0.073 / 0.033
```

✅ 媒体时间戳切分正确（0–5、5–10）、drop-oldest 未触发、推理耗时远低于实时。

### 2.2 真机端到端（8/8 窗口命中，中英均正确）

```
[23:26:39] llama-server ready
[23:26:40] worker final [ 0.16.. 5.16] lang=chinese rtf=0.150 :: 甚至出现交易几乎停滞的情况。嗯哼。
[23:26:40] 字幕[代次 0] 甚至出现交易几乎停滞的情况。嗯哼。
[23:26:40] worker final [ 5.17..10.17] lang=english rtf=0.102 :: Oh yeah yeah, he wasn't even that big when I started listening to him.
[23:26:43] worker final [10.18..15.18] lang=english rtf=0.085 :: But and his solo music didn't do overly.
[23:26:48] worker final [15.20..20.20] lang=chinese rtf=0.102 :: 出现交易几乎停滞的情况。嗯哼，哦， yeah yeah.
[23:26:53] worker final [20.21..25.21] lang=english rtf=0.084 :: He wasn't even that big when I started listening to him, but
[23:26:58] worker final [25.22..30.22] lang=english rtf=0.061 :: His solo music didn't do overly.
[23:27:03] worker final [30.24..35.24] lang=chinese rtf=0.084 :: 可以几乎停滞的情况。嗯哼，哦， yeah， yeah， 他。
[23:27:08] worker final [35.25..40.25] lang=english rtf=0.086 :: Wasn't even that big when I started listening to him, but and his solo.
```

- **字幕确实进入渲染链**：`字幕[代次 0]` 由 `取当前条目()` 写出，只在 `绘制扩展定时文字`
  事件里被调用 —— 有这些行即证明字幕已随帧上屏（与外部 SRT 同一图层，AI 行在其上一行）。
- **语言自动判别正确**：中文段 `chinese`、英文段 `english`，与素材交替结构一致。
- **末窗未触发**：素材 42.64 s，最后一窗应落在 40.25–45.25，末端不足一窗，符合预期。

### 2.3 延迟账

| 项 | 实测 |
| --- | --- |
| 模型加载（冷启动） | ≈ 10 s（08:29 → 08:39） |
| 首次字幕出现 | 开播后 ≈ 11 s（加载 10 s + 首窗推理 0.75 s） |
| 单窗推理耗时 | rtf 0.061–0.150 → 每 5 s 窗 **0.30–0.75 s** |
| 稳态上屏滞后 | ≈ **一次推理耗时**（0.3–0.75 s） |

关键点：模型加载期间 tap 环把音频缓冲住了，服务就绪后 worker 先把积压的两窗一次吐出，
随后按 5 s 节拍与播放实时对齐 —— **加载延迟是一次性代价，不累积**。
字幕文本覆盖 `[end-5s, end]`，因此画面上"当前这句话"比正在说的音晚一窗，这是 D5 的既定设计
（5 s 窗换准确率），不是缺陷；若要更短滞后需减窗长，代价是识别质量。

### 2.4 崩溃隔离（Kill worker）

23:34:31 强杀全部 `python.exe`（含 worker）：

```
[23:34:33] AI 字幕：worker 输出流结束
[23:34:33] AI 字幕：取数线程因写帧失败退出
=== AFTER（+22 s）
FFF.Player.exe  90292 Console  1  262,764 K     ← 播放器存活
（无 python.exe）
（无异常、无崩溃、无后续字幕行）
```

✅ **只关字幕，播放不受影响**：结果线程见 EOF → 状态机作废显示；pump 写帧失败 → 安静退出；
播放器 22 s 后仍在运行。符合 P4 门禁"AI 推理崩溃（Kill worker）只关字幕"。

---

## 3. P1.5 稳态显存（Q8_0 vs Q6_K 选型）

同会话、同参数（仅换模型），`nvidia-smi --query-gpu=memory.used` 取**增量**（本机有其他 GPU 占用，基线 5.0–5.2 GiB）：

| 量化 | 模型文件 | 显存增量 | 备注 |
| --- | --- | --- | --- |
| Q8_0 | 804.7 MB | **1991 MiB** | P1.4 15 分钟连续测试用的配置（181/181 窗零错误） |
| Q6_K | 622.7 MB | **1642 MiB** | 省 **349 MiB**（−17.5%） |

配合 P1.3 结论（Q6_K 与 Q8_0 实词 100% 一致、对 bf16 五窗全 0.00%）：
Q6_K 是本机的"免费"显存回收。**默认仍发 Q8_0** —— 它是跑满 15 分钟连续测试和全部 CER 门禁的配置；
Q6_K 作为显存紧张时的可选档（worker 已支持 `--quant`，宿主尚未暴露为设置项）。

> 待决：是否把 Q6_K 设为默认 / 是否在设置页暴露量化档位。当前倾向"首版不动"，留给用户裁定。

---

## 4. 真机才暴露的 3 个缺陷（已修）

### 4.1 🔴 tap 读块 marshal：非 blittable 结构被钉住

**现象**：pump 每轮抛 `ArgumentException: Object contains references. (Parameter 'value')`，
2 分钟 21320 次，tap 一帧都没送出去（字幕全空）。

**根因**：`原生音频Tap块` 含 `<MarshalAs(ByValArray)> 声道位置 As Integer()` —— 数组是引用，
结构体**非 blittable**，`GCHandle.Alloc(块, GCHandleType.Pinned)` 必然抛。
（`配置`/`统计` 两结构全是值类型，钉得住，所以只有读块炸。）

**修复**（`Interop/播放器原生接口.vb`）：改走非托管内存
`Marshal.AllocHGlobal` + `StructureToPtr` / `PtrToStructure`，由 marshaller 处理 ByValArray。
已核对 C 侧 `FFF3FPAudioTapBlock` 布局与 VB 完全一致（顺序、对齐、总长 144 字节）。

### 4.2 取数轮次异常日志无限流

一次持续失败就能在 2 分钟写 1.7 MB 日志。改为：首次必记，之后每 30 s 汇总一次
（带累计次数），退避从 5 ms 提到 50 ms。

### 4.3 worker 健康轮询走系统代理

`asr_worker.py` 只有转录 POST 用了 `trust_env=False`，健康轮询用模块级 `requests.get` ——
在有系统代理的环境里 localhost 会被代理接管返回 404，90 s 超时后 worker 判失败退出。
改为模块级无代理 session（`_HTTP`），健康检查与转录共用。

---

## 5. 环境异常（需要留意）

23:28 前后，`_3fp-ai-subtitles/third_party/vcpkg_installed/x64-windows/` 的
`bin` / `lib` / `debug` 内容被清空，同时 `3fp/LakeUI` 的构建产物 `LakeUI.dll` 消失
（deps.json / xml 还在），导致：

- LakeUI 类型全未定义（482 → 5 个编译错误连锁）；
- `DiscRuntime.props` 拷不到 `bluray-3/udfread-3/libxml2/iconv-2/charset-1.dll`（MSB3030）。

**已恢复**：从 `3fp/build/avs3-control/` 拷回 5 个 disc 运行时 DLL；`dotnet build LakeUI` 重建库。
播放器输出目录里的 DLL 未受影响。原因未查明（非本次改动所致），
建议后续留意是否有清理/杀软在动 `C:\PLAN` 下的 DLL。

---

## 6. 遗留与下一步

> 本节写于 P5 当时。后续几轮的结论直接标在原条目上，避免有人照着过期的坑去踩。

- ~~**P6**：README + venv 说明 + 提交整理。~~ → ✅ 已交付：`PACK_FORMAT.md`（包契约）、
  `USER_GUIDE.md`（装包/模型/启用/限制）、`BACKEND_MATRIX.md`（后端矩阵）。
- ~~待决：Q6_K 是否设为默认 / 是否暴露量化档位~~ → **缺口已闭合但决定仍未做**：
  包化之后清单的 `model` 字段指哪个 .gguf 就用哪个档（`PACK_FORMAT.md` §4），
  所以"宿主没暴露量化档位"不再是问题；剩下纯粹是"默认发哪一档"的产品决定
  （Q6_K 省 349 MiB、CER 等价）。当前仍默认 Q8_0。
- ~~已知限制：纯音频文件无视频轨 → `绘制扩展定时文字` 不触发 → AI 字幕不显示~~ →
  ✅ 已修（`5d8cc05`）：宿主 `无视频时也绘制`（默认关，装了包才开）+ 内核
  `subtitleLayoutEnabled_` latch。证据见 `USER_GUIDE.md` §6。
- **未测：seek / 暂停 / 切文件的字幕清屏表现**（P4 门禁剩余项）→ 仍未做完整矩阵实测。
  代码链路是通的（内核 tap 块盖代次 → 宿主 pump 每帧发 `generation` → worker
  `reset_generation()` 清缓冲；状态机另有陈旧守卫/代次守卫/跨时间条守卫），但缺实测证据。
- 后续新增（不在 P5 范围）：推理后端可选（`26a34dd`）、多实例端口冲突与日志滚动
  （见 `USER_GUIDE.md` §5）。
