# 第三方组件、许可与来源声明 / Third-Party Notices

适用范围：`FFF.Player`（3FP）、`FFF.Native.dll`、随包运行库与 AI 字幕扩展包。
本文件是**事实与分发边界清单**，不是法律意见；许可条款以各组件原文为准。
最后核对：2026-11-13。

## 0. 本仓自身

| 项 | 声明 |
|---|---|
| 项目 | FFF Project / 3FP |
| 版权人 | Copyright (c) 2026 1059 Studio |
| 许可 | MIT，全文见根目录 `LICENSE.txt` |
| 文件级标识 | 新增第一方源码建议使用 `SPDX-FileCopyrightText: 2026 1059 Studio` + `SPDX-License-Identifier: MIT`；历史文件不批量改写，避免制造无意义 diff |
| Windows 版本资源 | `FFF.Native.rc` 的 `LegalCopyright` 与 `LICENSE.txt` 一致 |
| 归属声明 | 根目录 `NOTICE`；第三方清单为本文件；文件级许可说明见 `LICENSING.md`；许可原文在 `licenses/` |

`third_party/` 下的上游代码、头文件、静态库和运行时**不适用**本仓 MIT；它们保留各自版权与许可，只按各自条款使用。

## 1. 分发形态与 FFmpeg 边界

- **3FP 本体**：`FFF.Player.exe`、`FFF.Native.dll`、本仓第一方代码与 .NET 运行依赖。**本体不内嵌 FFmpeg**；FFmpeg 是用户自备或另行分发的**外部/独立运行时**。
- **完整运行时包**：`发布3FP.ps1` 当前仍按完整包检查并复制 `avcodec-*` / `avformat-*` / `avutil-*` / `avfilter-*` / `swresample-*` / `swscale-*` 六个 DLL；这属于完整运行时包，不是本体的一部分。完整包必须按 FFmpeg 自身许可单独合规。
- 当前树 `runtime/` 的实测构建串含 `--enable-gpl --enable-version3 --enable-libx264 --enable-libx265 --enable-libfdk-aac`，其中六个 DLL 还含 `--enable-nonfree`。`third_party/ffmpeg/` 有 `COPYING.LGPLv2.1`、`COPYING.LGPLv3`；GPLv2/GPLv3 原文已补到 `licenses/GPL-2.0-only.txt`、`licenses/GPL-3.0-only.txt`（完整运行时包适用）。
- 因此：**本体包不得出现 FFmpeg DLL**；若另行分发完整运行时包，必须补齐对应 GPL/LGPL 文本、源码获取方式，并按 `--enable-nonfree` 构建的实际条件处理。`FFF_ALLOW_NONFREE=1` 只能作为显式承担后果的放行，不是正常公开 Release 开关。
- FFmpeg 许可说明：<https://ffmpeg.org/legal.html>。

## 2. libass 字幕栈（vcpkg 传递依赖，随完整包 12 个 DLL）

证据源：`third_party/vcpkg_installed/x64-windows/share/<port>/vcpkg.spdx.json` 的 `licenseConcluded` 与 `copyright` 文件。

| port | 许可 | 随包件 |
|---|---|---|
| libass | ISC | `ass-9.dll` |
| libbluray | LGPL-2.1-or-later | `bluray-3.dll` |
| libudfread | LGPL-2.1-or-later | `udfread-3.dll` |
| libiconv | LGPL（其 `COPYING.LIB` 自述） | `iconv-2.dll`、`charset-1.dll` |
| fribidi | LGPL-2.1-or-later | `fribidi-0.dll` |
| freetype | FTL OR GPL-2.0-or-later | `freetype.dll` |
| harfbuzz | MIT-Modern-Variant | `harfbuzz.dll` |
| brotli | MIT | `brotlicommon.dll`、`brotlidec.dll` |
| bzip2 | bzip2-1.0.6 | `bz2.dll` |
| libpng | libpng-2.0 | `libpng16.dll` |
| zlib | Zlib | `z.dll` |
| libxml2 | MIT | `libxml2.dll` |

LGPL 组件如随完整包分发，应保留许可文本，并提供对应源码/可重新链接的获取方式；该项仍未闭环，发布前按 `83` 报告 §9 处理。

## 3. 静态链进 FFF.Native.dll 的 libiamf 一族

`FFF.Native.vcxproj` 的 `HaveLibiamf` 分支链入 `iamf.lib; oar.lib; obr_capi.lib; obr.lib; pffft.lib; FLAC.lib; opus.lib` + 59 个 `absl_*.lib`。

| 组件 | 许可/状态 | 原文位置 |
|---|---|---|
| libiamf | BSD-3-Clause Clear（AOM，2022） | `licenses/libiamf-LICENSE.txt`、`licenses/libiamf-PATENTS.txt` |
| OAR | BSD-3-Clause Clear（AOM，2022） | `licenses/oar-LICENSE.txt`、`licenses/oar-LICENSE-main-branch.txt` |
| OBR | 随 oar 仓分发；上游无独立 LICENSE，按宿主仓 oar 的 BSD-3-Clause Clear 对待；具体授权含义未判定 | `licenses/OBR-NOTICE.txt`（说明性，不是替代许可） |
| FLAC | BSD-3-Clause（Xiph）；免版税公开授权 | `licenses/FLAC-BSD-3-Clause.txt` |
| opus | BSD-3-Clause（Xiph）；免版税公开授权；含专利许可说明 | `licenses/opus-BSD-3-Clause.txt` |
| pffft | FFTPACK/UCAR 条款（Julien Pommier 2013 + Hayati Ayguen 2020 修改） | `licenses/pffft-license-header.txt` |
| abseil-cpp（59 件） | Apache-2.0 | `licenses/abseil-cpp-LICENSE` |
| fdk-aac | 上游许可原文未随 libiamf 仓提供；当前链接闭包对其符号为零引用，**代码未进包** | `licenses/fdk-aac-NOTICE.txt`（说明，不是许可文本） |

若未来链接闭包变化使 fdk-aac 代码被抽取，必须先收原文、重新评估再发布；不要把“当前零进包”当成永久豁免。

## 4. LakeUI

- 本仓引用 `third_party/LakeUI`（`FFF.Player.vbproj` 的 `LakeUI` HintPath；`FFF.Recorder.vbproj` 的 `LakeUI.Notifications`）。
- 上游 `LICENSE.txt` 为 **GPL-3.0-only**，README 说明公开产品使用需取得许可。
- 项目所有者已确认**已有上游直接授权**；仓库内目前没有书面凭证。发布前应归档授权书/邮件/合同，并确认覆盖 `FFF.Player`、`FFF.Recorder`、`LakeUI.Notifications` 与当前分发方式。授权凭证不能由本清单替代。
- 许可原文：`licenses/LakeUI-GPL-3.0.txt`（由发布脚本从 `third_party/LakeUI/LICENSE.txt` 收集）。

## 5. NuGet 直接引用

| 包 | 许可 |
|---|---|
| `Svg 3.4.8` | MS-PL |
| `Vortice.Direct2D1 / Direct3D11 / DirectComposition / DirectX / DXGI 3.8.3` | MIT ×5 |

NuGet 包的完整许可文本按其 `.nuspec`/包内 LICENSE 保留；发布包应带 NuGet notices 汇总，目前仍需补齐。

## 6. AI 字幕（宿主内建 + 运行期第三方包）

AI 字幕的宿主侧集成代码（控制器、菜单、设置页、worker 启动与状态机）是第一方代码，按本仓 MIT 随本体分发；第三方模型与运行时由 `FFF.Player` 的下载器按用户选择从公开官方源组装，不随本体 exe 固定分发。组件与许可见 `tools/ai-subtitles/AI字幕包声明.txt`，许可原文在 `licenses/ai/`。这组许可均为宽松许可，允许嵌入/复制/再分发，但必须保留版权、许可、NOTICE 与来源说明。

| 组件 | 许可 | 来源 |
|---|---|---|
| Qwen3-ASR-0.6B（GGUF Q8_0 / ONNX int8） | Apache-2.0 | `https://huggingface.co/ggml-org/Qwen3-ASR-0.6B-GGUF`；`https://huggingface.co/csukuangfj2/sherpa-onnx-qwen3-asr-0.6B-int8-2026-03-25` |
| llama.cpp | MIT | `https://github.com/ggml-org/llama.cpp` |
| sherpa-onnx | Apache-2.0 | `https://github.com/k2-fsa/sherpa-onnx` |
| OpenVINO | Apache-2.0 | `https://github.com/openvinotoolkit/openvino` |
| Python 3.12 embeddable | PSF-2.0 | `https://www.python.org/` |
| NumPy | BSD-3-Clause | `https://numpy.org/` |
| ONNX Runtime | MIT | `https://github.com/microsoft/onnxruntime` |
| openvino_telemetry | Apache-2.0 | OpenVINO 包内组件 |

- 宽松许可不等于免通知：MIT/BSD/PSF/Apache 均要求保留版权、许可和 NOTICE（如有）。下载器**已实现**把 `AI字幕包声明.txt`、包根 `LICENSE.txt`（3FP 自有 worker/适配代码 MIT）与 `licenses/ai/` 随包复制，manifest 写入 `notice`/`license`；缺声明、许可目录或许可全文时下载器返回“安装不完整”。
- OpenVINO 遥测：`openvino_telemetry` 存在匿名使用统计；`Ai字幕管道.vb` 已在 worker 启动前**仅在 consent 文件不存在时写入 `0`（拒绝）**，尊重用户已有的 `1`（同意）选择；写入失败时静默，不阻断字幕。
- AI 生成字幕的界面标识与本地处理声明见 `83` 报告 §8.4；AI 生成内容标识按《人工智能生成合成内容标识办法》按场景评估。

## 7. 许可文本目录与发布门禁

- `licenses/`：本仓第一方收集的第三方许可原文（abseil、libiamf、OAR、pffft、FLAC、opus、OBR/fdk 说明、GPLv2/GPLv3、LakeUI GPL-3.0 等）。
- `licenses/ai/`：AI 扩展包用的 Apache-2.0、MIT、BSD-3-Clause、PSF-2.0 与 AI NOTICE。
- `tools/发布3FP.ps1` **已实现**在打包时收集 `NOTICE`、本文件、`LICENSING.md`、`licenses/` 与 vcpkg/FFmpeg/LakeUI 文本；缺件默认中止，`FFF_ALLOW_MISSING_LICENSES=1` 只能显式放行。
- 完整包/本体包分层：本体包出现 FFmpeg DLL 直接 fail；完整包按 FFmpeg 自身许可单独扫描。

## 8. 仍未闭环（不是“已合规”）

1. 完整运行时包的 FFmpeg 源码获取方式与 nonfree 构建条件；GPLv2/v3/LGPL 文本已补到 `licenses/`。
2. libass/LGPL 组件的 source offer 与可重新链接说明。
3. LakeUI 书面授权凭证归档；仓库内目前只有 GPL 文本。
4. OBR 独立授权文本、fdk-aac 原文（当前零进包）；若链接闭包变化需重新评估。
5. NuGet notices 汇总；AI `licenses/ai/` 已随构建输出验证，真实下载/运行与遥测实测仍待。
6. `THIRD-PARTY-NOTICES.md` 与 README 的维护：README 标注“AI 禁止修改”，由人工维护；本文件可随代码更新。
