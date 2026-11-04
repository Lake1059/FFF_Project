# 构建与发布脚本

需要 Windows、Visual Studio C++ v145 / Windows SDK、.NET 10 SDK，以及已按对应 `LakeUITfm` 构建的兄弟仓 `..\LakeUI\LakeUI`（Debug 配置）。脚本优先使用环境变量和 PATH 中的工具，其次查找正式版和 Preview Visual Studio。

在仓库根目录执行（PowerShell 7 或 Windows PowerShell 5.1）：

```powershell
.\tools\构建3FP.ps1                         # Debug
.\tools\构建3FP.ps1 -Configuration Release
.\tools\发布3FP单文件.ps1
.\tools\发布3FR单文件.ps1
.\tools\发布3FP.ps1                         # 包含运行依赖的文件夹
```

构建和发布会检查当前配置所需的 FFmpeg、libass、光盘库及 IAMF 文件，仅在缺失时调用准备脚本。修复 FFmpeg 开发文件时优先复用 `runtime` 中的现有 DLL。

- `-SkipDependencyPreparation`：只检查本地原生依赖，缺件直接报错，不调用准备脚本；`dotnet` 仍按正常流程还原 NuGet 包。
- `-SkipIamf`：显式禁用 IAMF，传入 `HaveLibiamf=false`；默认要求完整 IAMF 依赖。
- `-SkipTests`：构建时跳过测试工程的编译。目前 `FFF.Player.Tests` 尚未注册测试项，编译成功不代表运行回归测试。
- `-NetTFM` / `-LakeUITfm`：分别覆盖宿主和 LakeUI 的目标框架。
- 发布脚本的 `-OutputDirectory`：指定输出目录；相对路径以仓库根目录为准。

单独准备 IAMF：

```powershell
.\tools\准备Libiamf.ps1                      # Release
.\tools\准备Libiamf.ps1 -Configuration Debug
.\tools\准备Libiamf.ps1 -RepoPath C:\src\libiamf -BuildDirectory C:\build\libiamf -Parallel 8
```

源码固定到脚本中的提交，自动初始化子模块。只编译 IAMF 目标及其依赖，使用动态 CRT；Release（`/MD`）放入 `third_party\libiamf\lib\x64`，Debug（`/MDd`）放入其 `debug` 子目录，Abseil 各放入对应配置的 `absl` 子目录。两种配置不能混用。脚本可重复运行，保留 CMake 增量缓存；更换源码或生成器时应使用另一个 `-BuildDirectory`。

`构建Fmp4SeekProbe.ps1` 是独立探针入口，需要另行提供 `tests\Fmp4SeekProbe\main.cpp`；当前仓库未包含该源码。
