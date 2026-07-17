# 运行时目录

运行 `tools/准备FFmpeg.ps1` 后，本目录包含 FFF.Native 运行所需的四个 FFmpeg
shared DLL。DLL 会在构建 Recorder 项目时复制到托管程序输出目录。

本目录只服务本机开发和测试，四个 DLL 不进入软件发行包。运行软件的环境需自行在程序目录
放置 ABI major 匹配的一组 FFmpeg shared DLL。

高级用户只能用同一 FFmpeg major、同一构建中的 x64 shared DLL 成套覆盖这些文件，
并必须在覆盖后重启程序。替换第三方 DLL 等同于执行其中的代码，ABI 不兼容、缺少依赖
或恶意 DLL 均不在产品的稳定性保证范围内。
