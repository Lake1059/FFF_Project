# 构建与发布脚本

需要 Windows、Visual Studio C++ v145 / Windows SDK、支持目标框架的 .NET SDK（3FP 默认面向 .NET 11，3FR 默认面向 .NET 10），以及已按对应 `LakeUITfm` 构建的兄弟仓 `..\LakeUI\LakeUI`。`构建3FP.ps1` 默认读取 LakeUI 的 Release 产物，也可用 `-LakeUIConfig Debug` 指定已构建的 Debug 产物。脚本优先使用环境变量和 PATH 中的工具，其次查找正式版和 Preview Visual Studio。

在仓库根目录执行（PowerShell 7 或 Windows PowerShell 5.1）：

```powershell
.\tools\构建3FP.ps1                         # Debug
.\tools\构建3FP.ps1 -Configuration Release
.\tools\发布3FP单文件.ps1
.\tools\发布3FR单文件.ps1
.\tools\发布3FP.ps1                         # 包含运行依赖的文件夹
```

构建和发布会检查当前配置所需的 FFmpeg、LibRaw、libass、光盘库及 IAMF 文件，仅在缺失时调用准备脚本。修复 FFmpeg 开发文件时优先复用 `runtime` 中的现有 DLL。

直接用 Visual Studio 或 MSBuild 构建时，可以先运行 `准备LibRaw.ps1`。它校验固定版本的源码压缩包，构建启用 `USE_X3FTOOLS` 的 x64 动态库，并把头文件、导入库和 DLL 放在 `third_party\libraw`。

发布脚本会额外构建 `FFF.PrefetchHost.vcxproj`（前后预加载小助手，独立进程）。它由 `FFF.Player.vbproj` 的 `@(PrefetchHelper)` 随 `dotnet publish` 拷入交付目录；内核 `EnsureHelper()` 按自身 DLL 所在目录查找它；缺件时**只**降级 helper 侧的 WARM/PROBE，进程内静图解码线与静图缓存命中不受影响，播放不受影响。单文件发布形态（`发布3FP单文件.ps1`）按白名单只保留 `FFF.Player.exe`，不会携带这枚伴随 exe。

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

Debug 静态库与对应的编译器 PDB 一同暂存。构建脚本检测到缺少 PDB 时会重新准备 IAMF 依赖。
