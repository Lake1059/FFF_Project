# Licensing / 许可与归属

本文件说明 FFF Project / 3FP 的许可与第三方归属，不是法律意见；具体条款以
`LICENSE.txt`、`NOTICE`、`THIRD-PARTY-NOTICES.md` 和各组件许可原文为准。

## 1. 第一方代码

- 许可：MIT。
- 版权：Copyright (c) 2026 1059 Studio。
- 许可全文：根目录 `LICENSE.txt`。
- 适用范围：本仓自有源码、脚本、文档与资源，除非文件头或目录另有说明。
- 文件级标识：新增第一方源码建议写
  `SPDX-FileCopyrightText: 2026 1059 Studio` 与 `SPDX-License-Identifier: MIT`；
  历史文件不批量改写，避免制造无意义 diff。

## 2. 第三方与上游代码

`third_party/` 下的上游源码、头文件、静态库、运行时、模型和工具**不适用**
本仓 MIT。它们保留各自版权与许可，只按各自条款使用。不要把本仓 MIT 当作
第三方组件的再许可。

第三方清单、版本、来源和分发边界见 `THIRD-PARTY-NOTICES.md`；许可原文在
`licenses/`。AI 字幕扩展包另见 `tools/ai-subtitles/AI字幕包声明.txt` 与
`licenses/ai/`。

AI 字幕的宿主侧集成代码（控制器、菜单、设置页、worker 启动与状态机）属于第一方
代码，按本仓 MIT 使用；第三方模型/运行时只在其自身宽松许可（Apache-2.0、MIT、
BSD-3-Clause、PSF-2.0）条件下使用。该组许可允许嵌入、复制与再分发，前提是保留
版权、许可、NOTICE 与来源说明；因此宿主可以内建 AI 字幕能力，但必须保留
`AI字幕包声明.txt`、`licenses/ai/` 和界面上的“AI生成字幕”标识。

## 3. 分发时的归属规则

1. 散装本体包保留 `LICENSE.txt`、`NOTICE`、`THIRD-PARTY-NOTICES.md`、`ASSETS.md` 与 `licenses/`。
   单文件形态是明确例外：`发布3FP单文件.ps1` 只输出 `FFF.Player.exe`，不内嵌上述文件；单文件通过“关于”页的“项目主页 / GitHub”链接指向本仓主页，许可入口以仓库主页和本文件为准。
2. `发布3FP.ps1` 默认生成本体包，不包含外部 FFmpeg runtime；完整运行时包需显式
   `-IncludeExternalRuntime`，并按各自许可补齐文本、版权声明和源码获取方式。
   不要把“本体不内嵌 FFmpeg”当成“完整包无需处理”。
3. AI 字幕宿主集成随本体按 MIT 分发；第三方模型/运行时如随包嵌入，保留
   `AI字幕包声明.txt`、`licenses/ai/` 与 manifest 的 `notice`/`license` 字段；
   运行期下载包同样必须带这些文件。
4. 第一方界面图标、应用图标与默认背景图的来源见 `ASSETS.md`；新增美术资产必须
   同步登记来源/作者/许可，不得把来源不明的网络素材放入 `Resources` 或 `Assets`。
5. 第三方名称、商标、Logo 归各自权利人；本仓只做合理、惯常的归属说明，
   不授予商标许可。

## 4. 贡献

除非贡献者明确另行书面说明，提交到本仓的贡献按 MIT 许可纳入本仓；贡献者应确保
其提交有权按该许可提供，并保留必要的第三方归属信息。本仓目前不要求 CLA。

## 5. 维护

- `NOTICE`：版权人与第三方归属的入口。
- `THIRD-PARTY-NOTICES.md`：组件级事实清单，随依赖变化更新。
- `LICENSE.txt`：第一方 MIT 全文。
- `licenses/`、`licenses/ai/`：许可原文与说明，不替代合同/法务审查。
