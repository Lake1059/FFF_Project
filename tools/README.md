# 构建与发布脚本

需要 Windows、Visual Studio C++ v145、Windows SDK、.NET 11 SDK 和同级 `..\LakeUI` 中包含渲染后端配置的 LakeUI 源码。3FP 和 3FR 通过项目引用自动构建 LakeUI。脚本优先使用环境变量和 PATH 中的工具，其次查找正式版和 Preview Visual Studio。

在仓库根目录执行（PowerShell 7 或 Windows PowerShell 5.1）：

```powershell
.\tools\构建3FP.ps1                         # Debug
.\tools\构建3FP.ps1 -Configuration Release
.\tools\发布3FP单文件.ps1
.\tools\发布3FR单文件.ps1
.\tools\发布3FP.ps1                         # 散装本体包
.\tools\发布3FP.ps1 -IncludeExternalRuntime # 散装包 + 外部 FFmpeg DLL
```

构建和发布会检查当前配置所需的 FFmpeg、LibRaw、libass、光盘库及 IAMF 文件，仅在缺失时调用准备脚本。FFmpeg DLL 统一位于 `third_party/ffmpeg/bin/x64`。`准备FFmpeg.ps1` 优先复用本地整套 DLL 来生成导入库；首次下载或 `-ForceDownload` 使用 LGPL shared 变体，可能减少编解码能力，应把变体更换作为单独决策。已有旧布局的工作区可把根目录 `runtime` 中的六个 FFmpeg DLL 移到这个位置。

`准备Libass.ps1` 根据同一份 vcpkg 清单准备 libass 和光盘库。`准备LibRaw.ps1` 将启用 `USE_X3FTOOLS` 的 x64 动态库暂存到 `third_party/libraw`。Git 跟踪 `third_party` 中的 FFmpeg 和光盘运行库清单，以及 libbluray overlay 的 `portfile.cmake` 和 `vcpkg.json`。FFmpeg、LibRaw、libass、光盘库和 IAMF 的安装树由准备脚本在本机生成。

3FP 散装发布会携带 `FFF.PrefetchHost.exe` 独立预取进程。单文件发布只生成 `FFF.Player.exe`，由它展开必要的原生 DLL。两种发布形态都可从内置资源组装 AI 字幕扩展包。单文件打包会校验原生 DLL 清单和 AI 字幕资源清单，并要求暂存目录只含播放器 EXE。框架版本记录保存在输出目录旁。3FP 和 3FR 的单文件 EXE 各自选择 .NET 运行时，版本不同时发布脚本提示并继续。若需强制同目录版本一致，在 PowerShell 中传入 `-AllowFrameworkMismatch:$false`。

- `-SkipDependencyPreparation`：只检查本地原生依赖，缺件直接报错，不调用准备脚本；`dotnet` 仍按正常流程还原 NuGet 包。
- `-SkipIamf`：显式禁用 IAMF，传入 `HaveLibiamf=false`；默认要求完整 IAMF 依赖。
- `-SkipTests`：构建时跳过测试工程的编译。`FFF.Player.Tests` 通过命令行选项运行不同的回归套件。
- `-NetTFM` 可覆盖宿主的默认 Windows 目标框架，需与 LakeUI 项目兼容。3FR 默认使用 `net11.0-windows10.0.26100.0`。发布脚本要求 .NET 11 和显式 Windows SDK 版本，包内保留屏幕捕获所需的 WinRT 投影。
- 发布脚本的 `-OutputDirectory`：指定输出目录；相对路径以仓库根目录为准。

单独准备 IAMF：

```powershell
.\tools\准备Libiamf.ps1                      # Release
.\tools\准备Libiamf.ps1 -Configuration Debug
.\tools\准备Libiamf.ps1 -RepoPath C:\src\libiamf -BuildDirectory C:\build\libiamf -Parallel 8
```

源码固定到脚本中的提交，自动初始化子模块。只编译 IAMF 目标及其依赖，使用动态 CRT；Release（`/MD`）放入 `third_party\libiamf\lib\x64`，Debug（`/MDd`）放入其 `debug` 子目录，Abseil 各放入对应配置的 `absl` 子目录。两种配置不能混用。脚本可重复运行，保留 CMake 增量缓存；更换源码或生成器时应使用另一个 `-BuildDirectory`。

Debug 静态库与对应的编译器 PDB 一同暂存。构建脚本检测到缺少 PDB 时会重新准备 IAMF 依赖。
