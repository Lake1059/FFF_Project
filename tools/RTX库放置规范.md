# RTX Video SDK 库放置规范

3FP 的 RTX Video 支持只构建 `x64`。仓库不提交 NVIDIA SDK 二进制文件；开发者需要从已获授权的 RTX Video SDK 安装包中准备以下文件。

## 目录结构

文件应放在仓库的 `tools\RTX` 目录下，目录结构必须保持如下形式：

```text
tools\RTX\
  nvngx_vsr.dll
  nvngx_truehdr.dll
  include\
    nvsdk_ngx.h
    nvsdk_ngx_defs.h
    nvsdk_ngx_defs_truehdr.h
    nvsdk_ngx_defs_vsr.h
    nvsdk_ngx_helpers.h
    nvsdk_ngx_helpers_truehdr.h
    nvsdk_ngx_helpers_vsr.h
    nvsdk_ngx_params.h
  lib\Windows\x64\
    nvsdk_ngx_d.lib
    nvsdk_ngx_d_dbg.lib
```

项目实际引用以下 8 个头文件（按依赖关系复制即可）：

- `nvsdk_ngx.h`
- `nvsdk_ngx_defs.h`
- `nvsdk_ngx_defs_truehdr.h`
- `nvsdk_ngx_defs_vsr.h`
- `nvsdk_ngx_helpers.h`
- `nvsdk_ngx_helpers_truehdr.h`
- `nvsdk_ngx_helpers_vsr.h`
- `nvsdk_ngx_params.h`

项目实际链接的库只有：

- Release：`nvsdk_ngx_d.lib`
- Debug：`nvsdk_ngx_d_dbg.lib`

`nvsdk_ngx_s.lib`、`nvsdk_ngx_s_dbg.lib` 是静态链接库，不属于本项目构建依赖，不要放入发布包。

## 文件来源

推荐使用与当前实现匹配的 RTX Video SDK 1.1.0，并确认文件来自 Windows x64 SDK 目录：

```text
<SDK>\include\*.h
<SDK>\lib\Windows\x64\nvsdk_ngx_d.lib
<SDK>\lib\Windows\x64\nvsdk_ngx_d_dbg.lib
<SDK>\bin\Windows\x64\rel\nvngx_vsr.dll
<SDK>\bin\Windows\x64\rel\nvngx_truehdr.dll
```

运行时只将上面两个 `nvngx_*.dll` 作为 native payload 嵌入 RTX 单文件；不要把 `include`、`lib`、`samples`、`doc` 或其他 DLL 复制到最终发布目录。

## 构建方式

普通版本和 RTX 版本使用两个互不影响的发布入口。普通版本不探测、不链接、也不打包 RTX SDK：

```powershell
.\tools\发布3FP单文件.ps1 -Configuration Release
```

RTX 版本单独使用 `发布3FP单文件RTX.ps1`，并且必须传入 NVIDIA 分配的 Application ID：

```powershell
.\tools\发布3FP单文件RTX.ps1 `
  -Configuration Release `
  -RtxVideoSdkRoot "C:\path\to\RTX_Video_SDK" `
  -RtxVideoApplicationId <ApplicationId>
```

RTX 脚本默认优先使用完整的 `tools\RTX`，否则读取 `NV_RTX_VIDEO_SDK`。两个脚本都输出到 `publish\win-x64`，但只替换自己负责的文件：

```text
FFF.Player.exe       # 发布3FP单文件.ps1 生成
FFF.Player.RTX.exe   # 发布3FP单文件RTX.ps1 生成，内含两个 nvngx feature DLL
```

每个脚本只负责自己的 EXE，不会因为普通版本发布而删除已有的 RTX EXE，反之亦然。最终发布目录不应出现 loose DLL、`.lib` 或头文件。

如果只需要调试而不发布单文件，可在项目构建时同时传入 `-p:RtxVideoEnabled=1`、`-p:RtxVideoSdkRoot=<SDK路径>` 和 `-p:RtxVideoApplicationId=<ApplicationId>`；项目会把两个 feature DLL 复制到对应的 `bin` 输出目录，供开发态 NGX 搜索。普通构建或 `RtxVideoEnabled=0` 不会把 RTX DLL 纳入应用运行时。

## 运行验证说明

本项目直接调用 RTX Video SDK，不经过 NVIDIA App 支持播放器的驱动注入路径。因此 NVIDIA App 可能仍显示“未激活”，视频画面右上角也不会出现 NVIDIA RTX 水印；这两项不是本项目 SDK 集成是否成功的判据。应通过播放器的 RTX 状态 API 或调试诊断确认 `initialized`、能力字段、`active` 和评估帧计数。

NVIDIA App 的驱动注入/识别名单和状态水印没有面向第三方播放器的公开 API，不能通过修改窗口尺寸、进程名或普通 NVAPI 调用强制加入该路线。要获得该路线的行为，需要 NVIDIA 对播放器和驱动集成进行支持；在此之前只能使用本项目的公开 RTX Video SDK 路径。

RTX 只处理普通 2D SDR 视频：

- VSR 会在目标大于、等于或小于源尺寸时尝试执行；SDK 不接受缩小目标时，内部保持至少源尺寸，再由播放器缩放到窗口。这样可以在同尺寸或小窗口中尝试恢复/锐化，但不能保证每个 SDK 版本都接受该输入。
- TrueHDR 仅在 SDR 源进入 HDR 输出时执行。
- HDR10、HLG、Dolby Vision 等已是 HDR 的源视频继续使用原有 HDR 路径，不会重复进入 TrueHDR。

开启 RTX 后，SDR 源会单独请求 HDR 呈现合同以便尝试 TrueHDR；这不会改写公开快照中的 SDR 色彩模式。显示器不支持 HDR 或 SDK 不可用时，播放器自动回到原有 SDR 路径。

例如 3840x2160 的 HDR10 视频仍不会进入本项目的 RTX 路径，因为它已经是 HDR 源；这与窗口尺寸无关。要验证 VSR，应优先使用 SDR 视频并将窗口或渲染目标设置为大于源尺寸，以便直观看到放大细节。若 SDK 初始化返回 `FAIL_InvalidParameter`，需要向 NVIDIA 申请并在构建时传入有效的 `RtxVideoApplicationId`；不能用 NVIDIA App 的“激活”开关替代 SDK 授权。

## Git 与许可证

`tools\RTX\*.dll`、`tools\RTX\include\` 和 `tools\RTX\lib\` 已加入 `.gitignore`。这些文件不会被 Git 跟踪，也不会随源代码提交；每位开发者需按自己的 SDK 授权和分发许可准备本地副本。SDK 的许可证、文档和样例仍应保留在 NVIDIA SDK 原始目录中，不要复制到发布目录。
