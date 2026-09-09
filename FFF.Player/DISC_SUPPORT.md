# 光盘播放

## 发布合同

3F 自有源码使用 MIT，FFmpeg Shared DLL 由用户整套提供，不随 3FP 发布。
其他原生依赖沿用 libass 的单文件嵌入及运行时缓存释放机制。

光盘依赖由根目录 vcpkg manifest 固定版本，使用 `tools/准备光盘库.ps1` 准备。
libdvdnav、libdvdread 以及 vcpkg 引入的 libdvdcss 使用 GPL-2.0-or-later；
libbluray 和 libudfread 使用各自的 LGPL 许可证。发行时必须保留实际构建版本的
许可证和源码获取信息。3F 源码的 MIT 授权不把这些库或包含它们的组合发行物改为 MIT，
也不能通过动态链接或 EXE 自解压免除相应义务。

## 技术验证

原生探针为 `FFF.Player.Tests/Native/DiscNavigationProbe.cpp`，独立调用导航库。
它检查 DVD 菜单按钮及激活后的标题转换，以及蓝光导航事件、图形覆盖回调和激活命令。
探针的快速读取和跳过静帧只用于库能力检查，不是播放器最终的呈现和计时实现。

```powershell
./tools/测试3FP光盘导航.ps1 -DvdIso '<DVD ISO 完整路径>' -BlurayPath 'E:\'
```

结果保存在 `artifacts/disc-probe`。只有素材解码成功不代表菜单验证通过。
必须分别验证菜单导航、菜单图形实际合成、输入坐标、跳转后音视频及退出释放。

播放管线回归使用 `FFF.Player.Tests --disc-regression <来源> <结果目录>`。设置
`FFF_DISC_TEST_GPU=1` 可使用 GPU 配置。测试使用隐藏 HWND 和 D3D11 内部回读，
禁止屏幕截图；`menu-internal.png` 是完整合成帧，`menu-overlay-internal.png` 是菜单层，
`highlight-internal.png` 验证方向键高亮变化，`menu-portrait-internal.png` 验证缩放。
设置 `FFF_DISC_TRACE=1` 可输出导航事件诊断。

## 当前接口与范围

2026-09-09 导航修正：滑条在光盘正片中使用导航时间跳转，暂停时同步更新预览帧。
蓝光执行 First Play 后优先进入可用顶层菜单。HDMV 输入使用零长度读取推进导航 VM，
仅真实播放列表／时间轴变化才重建解复用器，章节等纯图形子菜单保留背景与静帧。
失败状态停止接受排队输入，鼠标移动不重复弹错误框。
`--disc-slider-regression <DVD路径>` 覆盖控制器的滑条调用路径；光盘回归新增
自动菜单、章节子菜单、连续悬停和返回按钮检查，图像均由内部 GPU 回读生成。

Native API 14 提供光盘导航、光盘状态与 SDR 合成帧回读；托管入口为
`播放器会话.光盘导航`、`当前光盘状态`、`读取SDR合成帧`。
标题栏菜单可异步扫描并打开可用光驱，并在光盘模式下提供根菜单和弹出菜单；ISO 沿用文件打开／拖放。
菜单可见时方向键、Enter、返回键及鼠标优先交给导航库。流选择器仅列媒体流，
不展开光盘标题／章节，章节选择交给光盘原生菜单。

DVD 的版权等无按钮菜单域也采用单帧低延迟解码；菜单单元切换重建时间轴，避免
版权帧因沿用片头时间戳被丢弃。DVD 按钮 PCI 随 NAV 数据保留，等待期间输入与高亮
使用同一份已呈现菜单数据，不因导航库预读推进到下一域而丢失焦点。
片头没有当前 VTS 菜单时，“根菜单”先建立标题上下文再调用根菜单。
`--disc-edge-regression <来源> <目录>` 检查版权帧、30 秒菜单停留后的焦点和
蓝光第二菜单项／第二章节；`FFF_DISC_EARLY_MENU=1` 验证片头期间转入菜单。

DVD 菜单的单帧 MPEG-2 使用低延迟输出，避免背景延迟到 EOF，导致音频预缓冲被跳过、
整段菜单快速读完。普通文件和 DVD 正片仍使用原有解码策略。读取结束区分导航等待和
真正结束，跳转会重新探测媒体流并重置队列与时钟。逐帧剪辑不适用于光盘导航。

2026-09-08 至 09 实机覆盖：用户 DVD ISO 和 E: 中的 HDMV 蓝光盘。两者 CPU/GPU 配置均通过
菜单背景、按钮高亮变化、确认进入正片、暂停恢复、60 秒跳转、返回根菜单、竖向缩放与鼠标激活。
此回归的菜单点击坐标针对这两份实机样本，不是任意光盘的通用自动菜单遍历。
其他验证包括共享文件打开、WASAPI 共享／独占延迟和零欠载、SDR/HDR 覆盖层数值、
主窗体打开期间退出。Debug 与 Release x64 构建均通过。
这不代表所有光盘已兼容；多角度、复杂跨片段无缝拼接、UHD 和 BD-J 尚未建立完整回归覆盖。
单文件探针 `--disc-publish-smoke <来源>` 验证了导航 DLL 来自 .NET 解压缓存，
FFmpeg DLL 来自外部目录。`FFF3FP_CopySdrFrame` 目前仅支持 BGRA8 SDR，不将 HDR 原始值
错误转换成截图；现有单像素探针继续用于 HDR 数值校验。

## 构建来源

根目录 `vcpkg-configuration.json` 指向 `tools/vcpkg-ports`。VideoLAN GitLab 归档端点
可能返回反爬 HTML，项目改用 `download.videolan.org/pub/videolan` 官方版本归档，
下载时验证固定 SHA-512；版本分别为 libbluray 1.4.1、libdvdnav 6.1.1、libdvdread 6.1.3、
libdvdcss 1.5.0。libudfread 1.2.0 由 manifest baseline 解析。

DVD 的 Windows 补丁以 baseline 中 vcpkg 的 MSVC 适配为基础；发行归档不包含完整
MSVC 兼容目录，补充最小标准头映射。IFO 字节位域使用 `unsigned char` 存储，避免
MSVC 即使 pack(1) 也为不足 32 位的 `unsigned int` 位域分配四字节，破坏标题和单元表。
生产代码验证 `playback_type_t` 为 1 字节、`title_info_t` 为 12 字节。

当前 libbluray 构建明确禁用 BD-J JAR。HDMV、DVD 菜单与 BD-J 是不同能力；遇到需要
BD-J 或未具备解密支持的加密盘应返回具体错误，不能宣称已经提供完整蓝光兼容性。
