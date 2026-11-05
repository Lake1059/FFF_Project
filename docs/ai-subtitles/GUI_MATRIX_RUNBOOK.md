# AI 字幕 GUI 交互矩阵 · 实机执行单

> 这份是**执行顺序**，判据与勘误都在 `SMOKE_CHECKLIST.md`（31 项表格 + 判据勘误
> 小节）。那边写"验什么、怎么判"，这边写"按什么顺序点、点之前准备什么"。
>
> 为什么只能人点：`cscript`/`wscript` 被安全策略当 LOLBin 拦掉，`SendKeys` 发不
> 出去；schtasks 派发的窗口又不在 gdigrab 能抓的桌面上，截屏取证也不可行。所以
> 证据全靠日志（`字幕[代次 N]`、`generation -> N`、`字幕图层状态 …`），我这边有
> `_3fc_tmp/judge_matrix.py` 自动判读。

## 0. 环境（已就绪，2026-10-25）

| 项 | 位置 |
|---|---|
| 播放器（07:44 重建，0 警告 / 0 错误） | `C:\PLAN\3FCompare\_3fp-ai-subtitles\FFF.Player\bin\Release\net10.0-windows10.0.26100.0\FFF.Player.exe` |
| 主日志（随验收增长） | 同目录 `ai-subtitles.log`（旧日志已改名 `.bak-20261025`，14 个 `asr_worker_*` 残留已清） |
| 扩展包目录（默认） | `E:\WorkBuddy\3FP-AI-Subtitles\3FP-AI-Subtitles-Pack`（worker 已同步到 20261025e） |
| `llama-server.log`（第 24 项看它） | 扩展包目录下（worker 写在脚本旁） |
| 素材（带人声） | `C:\PLAN\3FCompare\tests\media\video\speech\`：en-lecture(264s) / en-interview(251s) / zh-en-mix(399s) / zh-interview(76s) / zh-wiki(198s，弱语音) |
| 无音轨素材（第 7 项） | `C:\PLAN\3FCompare\tests\media\video\real\real_720p_h264_derived.mp4`（12 s，ffprobe 确认无音轨） |
| 变体包（第 21/22/23/24/26/30 项用） | `E:\WorkBuddy\3FP-AI-Subtitles\_packs\`：`layers32` / `args2048` / `args4096` / `single-tier` / `nommproj` / `badweight` |

**seek 用例避开 `zh-interview.webm`**（AV1，某些时间点抽不出音频 → 空 wav → HTTP 400）。
第 2/3 项用 `en-lecture.webm` 或 `zh-en-mix.webm`。

## 1. 通用节奏

1. 双击上面的 `FFF.Player.exe`（**不要**从我这边启动；沙箱注入会让走 DXGI 的子进程崩）。
2. 每批开跑前把 `ai-subtitles.log` 改名成 `ai-subtitles.log.batchA` 之类，让每批日志干净。
3. 一批点完，别关播放器也别急着下一批：喊我，我跑 `judge_matrix.py` 判一遍，
   有 MISS 就地补做（那时播放器还开着，补最省事）。
4. 需要进程证据时我自己 `tasklist` 查 `python.exe` / `llama-server.exe`。

## 2. A 批：基本清屏（第 1–9 项，约 5 分钟）

素材：`en-lecture.webm`。开播后**最多等 90 s** 出首句（冷启动前 1~2 窗各 57~60 s，别当成卡死）。

| 顺序 | 动作 |
|---|---|
| 1 | 打开 `en-lecture.webm`，等首句字幕（≤90 s），记下出现的时刻 |
| 2 | 按 `→` 前进 seek，看旧字幕是不是**一帧内**消失 |
| 3 | 继续播 20 s，看新字幕内容对不对得上新位置 |
| 4 | 空格暂停 10 s，字幕应保持可见不闪掉 |
| 5 | 再空格继续，确认任意时刻只有一条 AI 字幕 |
| 6 | **先 seek 到 60 s 之后**，再打开 `zh-en-mix.webm`（切文件），看旧字幕立刻消失 |
| 7 | 再切 `real_720p_h264_derived.mp4`（无音轨），应不出字幕、播放正常 |
| 8 | 播放中关掉 AI 字幕开关，字幕消失 |
| 9 | 关播放器（我先查进程残留） |

## 3. B 批：重启 / 语言 / 包（第 10–24 项，约 20 分钟）

- **第 10/11 项各连做 5 次**（停止→再播 / 关开关→开）：这两条是 P0 回归，代码层有
  一个竞态（旧 result 线程的 `Finally` 可能在重置之后又把"worker 已退出"置回去），
  单次通过证明不了什么。
- 第 12/13/18/19 项每次改设置都会重启 worker → **每次都要重付冷启动**，出字按 2 分钟
  内算，不是 10 s。
- 第 21–24 项要换包目录：设置页「扩展包目录」填 `E:\WorkBuddy\3FP-AI-Subtitles\_packs\xxx`，
  填完把焦点移出文本框（Leave 才应用）。
- 第 24 项（坏权重）跑完，我会去读 `_packs\badweight\llama-server.log` 确认有 llama-server
  的真实报错，并查有没有孤儿进程。

## 4. C 批：设置页新控件（第 25–31 项，约 15 分钟）

第 26 项要先用 `args4096` 包（清单 `--ctx-size 4096`），用户框填 `--ctx-size=8192`；
第 27 项用户框填四行：`# 这行是注释` / 空行 / 纯空格行 / `--metrics`。
第 30 项用 `layers32` 包。第 28/29/31 项以目视为主，我会把相关日志行打出来对照。

## 5. 判读

```bat
python C:\PLAN\3FCompare\_3fc_tmp\judge_matrix.py <ai-subtitles.log> a
python C:\PLAN\3FCompare\_3fc_tmp\judge_matrix.py <ai-subtitles.log> b --pack E:\WorkBuddy\3FP-AI-Subtitles\3FP-AI-Subtitles-Pack
python C:\PLAN\3FCompare\_3fc_tmp\judge_matrix.py <ai-subtitles.log> c
```

输出 `HIT` / `MISS` / `目视` 三态：`MISS` 不等于失败（可能没跑到），但每个 `MISS`
都要给出解释才收工。
