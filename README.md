# FFF 游戏录制核心

本仓库按计划保持两个构建项目：`FFF.Recorder` 是 VB.NET WinForms/托管功能层，
`FFF.Native` 是 C++/FFmpeg/WASAPI 稳定 C ABI。UI 交互不在本次实现范围内。

## 已实现功能

- WGC HWND 捕获与 DXGI Desktop Duplication 显示器捕获。
- D3D11 纹理池、单 GPU 命令线程、CFR 丢帧/重复帧调度。
- SDR 缩放/裁剪和 FP16 scRGB 到 HDR10 PQ，显式 HDR 到 SDR tone mapping。
- NVENC D3D11 零 CPU 回读编码，实际初始化能力探测与进程内缓存。
- CQP/VBR/CBR、最大码率、GOP、B 帧、Lookahead、Preset、Profile 和多遍参数。
- 事件驱动 WASAPI、48 kHz AAC、双源混音或独立轨、增益/静音和漂移补偿。
- 独立 Matroska packet 写线程、正常 drain/trailer、JSONL 诊断和统计快照。

所有托管公开方法和参数使用中文；C++ 标识符保持英文，方法定义和头文件维护说明使用中文。

## 构建

```powershell
& 'C:\Program Files\Microsoft Visual Studio\18\Insiders\MSBuild\Current\Bin\amd64\MSBuild.exe' `
  'FFF.Native\FFF.Native.vcxproj' /p:Configuration=Release /p:Platform=x64 /m
dotnet build 'FFF.Recorder\FFF.Recorder.vbproj' -c Release
```

Native 必须先构建；开发构建会从本机 `runtime` 目录复制四个固定 major 的 FFmpeg DLL，
用于编译后测试。它们不属于软件发行包。
启动时 `录制引擎.验证原生接口()` 检查产品 C ABI v2。

## FFmpeg 运行约束

发行包不包含 `avcodec-63`、`avformat-63`、`avutil-61` 和 `swresample-7`。运行环境必须自行
在程序目录提供来自同一次 Windows x64 shared 构建、major 匹配且包含 Matroska、AAC 和所需
硬件编码器的 DLL。缺失或架构错误会由托管层转换为可读的加载错误。

QSV 的 D3D11→QSV 派生设备、surface 映射和 Video Processor 转换路径已经实现并通过编译与
静态逻辑检查；因当前机器没有 Intel GPU，尚未做 QSV 实机编码验证。AMF 的 D3D11 路径也已
实现并完成静态检查；当前机器没有 AMD GPU。运行时缺少匹配适配器、oneVPL 或 `amfrt64.dll`
时，真实探测会返回底层原因，不会把仅存在编码器名称误报为可用。

## 单文件发行

运行 `tools/发布单文件.ps1` 会生成 framework-dependent `win-x64` 单文件程序，托管依赖会
打包进 `FFF.Recorder.exe`。脚本明确拒绝发行目录中的四个 FFmpeg 运行库；运行环境需自行提供。
产品自有 `FFF.Native.dll` 会优先嵌入单文件，若 SDK 无法嵌入则允许作为唯一额外产品文件保留。
