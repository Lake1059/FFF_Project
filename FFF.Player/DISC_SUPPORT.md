# Blu-ray support

3FP 播放器支持 Blu-ray 光驱根目录及 `BDMV` 目录（也可直接选择 `.bdmv` 文件）。导航、HDMV 菜单、图形覆盖和章节由 libbluray 提供。

构建依赖为 libbluray 与 libudfread；发行时需随程序提供对应运行库及许可证文本。当前构建不包含 BD-J 支持，依赖 BD-J 的光盘会显示错误。
