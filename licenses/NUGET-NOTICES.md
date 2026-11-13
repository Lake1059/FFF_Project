# NuGet 组件许可与通知

本文件汇总 3FP 直接引用与传递依赖的 NuGet 组件。完整许可文本见同目录 `MS-PL.txt`、`MIT.txt`；包内如另有版权行，以包内原文为准。

| 包 | 版本 | 许可 | 角色 | 包内版权行（`.nuspec`） |
|---|---|---|---|---|
| `Svg` | 3.4.8 | MS-PL | 直接引用；构建期图标烘焙/兼容保留 | `© Copyright 2009 Microsoft. All Rights Reserved. © Copyright 2011 vvvv Group. All Rights Reserved. © Copyright 2021 SVG.NET Contributors` |
| `Vortice.Direct2D1` | 3.8.3 | MIT | 直接引用；D2D 互操作 | `Copyright (c) Amer Koleci and Contributors` |
| `Vortice.Direct3D11` | 3.8.3 | MIT | 直接引用；D3D11 互操作 | `Copyright (c) Amer Koleci and Contributors` |
| `Vortice.DirectComposition` | 3.8.3 | MIT | 直接引用；DirectComposition 互操作 | `Copyright (c) Amer Koleci and Contributors` |
| `Vortice.DirectX` | 3.8.3 | MIT | 直接引用；DirectX 基础 | `Copyright (c) Amer Koleci and Contributors` |
| `Vortice.DXGI` | 3.8.3 | MIT | 直接引用；DXGI 互操作 | `Copyright (c) Amer Koleci and Contributors` |
| `ExCSS` | 4.2.3 | MIT | `Svg` 传递依赖 | 包内 `copyright` 为空；作者 `Tyler Brinks` / ExCSS contributors |
| `SharpGen.Runtime` | 2.4.2-beta | MIT | `Vortice` 传递依赖 | `(c) 2010-2017 Alexandre Mutel, 2017-2023 Jeremy Koritzinsky, 2023-2024 Amer Koleci` |
| `SharpGen.Runtime.COM` | 2.4.2-beta | MIT | `Vortice` 传递依赖 | `(c) 2010-2017 Alexandre Mutel, 2017-2023 Jeremy Koritzinsky, 2023-2024 Amer Koleci` |

`Svg` 的 `MS-PL` 文本见 `licenses/MS-PL.txt`；MIT 组件文本见 `licenses/MIT.txt`。`MIT.txt` 是 MIT 许可模板，真正的版权行以上表 `.nuspec` 原文为准；发布包应同时保留本文件、上述许可文本和各包 `.nuspec` 中的版权/作者信息。

本文件是通知汇总，不构成法律意见；如与包内许可原文冲突，以包内原文为准。
