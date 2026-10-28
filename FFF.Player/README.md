# FFF.Player / 3FP

3FP 是 Windows 本地播放器子项目，入口是“播放器会话”，界面宿主是“FFF.Player”。播放器通过 FFmpeg Shared DLL 解码，由 FFF.Native 的 D3D11/WASAPI 管线呈现视频、音频、图片、字幕、歌词和弹幕。

## 支持范围

- Windows 10 2004 及以上，x64，.NET 10 Desktop Runtime。
- 本地文件、Blu-ray 光驱根目录、BDMV 目录和 .bdmv 文件。
- 视频、音频和静态或多帧图片；不提供 ISO、DVD、网络协议、管道或视频帧服务器。
- SRT、ASS/SSA、SUP/PGS 字幕；LRC 歌词；B 站 XML 弹幕。
- CPU/GPU 解码。GPU 后端按驱动实际能力选择 CUDA/NVDEC 或 D3D11VA，失败时回退 CPU。
- WASAPI 共享与独占模式，音频位置以 IAudioClock 为准。
- SDR、HDR10、HDR10+、HLG 以及 HDR 到 SDR 映射。实际模式通过播放器快照和色彩模式事件报告。

## 渲染与文字图层

视频统一使用自有 D3D11 shader；CPU 解码帧和硬件帧共用色彩、缩放和合成路径。HDR 输出使用线性 scRGB FP16，SDR 输出使用 BGRA8 或 RGB10A2。

字幕、歌词和弹幕使用独立托管图层槽位，最终由一个 Native 呈现线程按“视频、弹幕、字幕、歌词、信息”顺序合成。弹幕呈现器读取窗口所在显示器的实际刷新率，目标帧率限制为 60 到 120 FPS；移动窗口或显示设置变化时重新探测。目标帧率只控制图层节拍，滚动位置始终使用连续媒体时钟。弹幕描边采用更宽的外描边，阴影使用更深的不透明度和三倍扩散边界，稳定文字在 GPU 图集中复用。

歌词字体、字号和样式由“设置 → 音乐与歌词”保存；弹幕字体和渲染细节由“设置 → 弹幕”保存。个性化页可设置窗口边框、分层阴影，以及主窗体 JustEmptyControl1 和标题栏 CaptionBottomLine 的颜色与宽度。

窗口进入系统移动循环时保留待上屏的视频帧，并将音频队列目标至少提高到 120 ms；欠载等告警仍可按需增加储备，换文件或音轨时重置。移动态的重复图层合成最高为 60 FPS，电影帧按自身时间戳提交。SRT 重叠条目分配到顶部，弹幕图集采样限制在各自精灵矩形内。

定位后的音频预卷逐帧裁到目标时间，再建立连续 PCM 时间线；抖动诊断包含重采样器内待输出样本，并允许 5 ms 的短时量化偏差。信息层的“抖动”统计反映超过容差的音频时间戳偏差，欠载另行统计。顶部字幕各行统一使用主字幕第一行的字体、字号、样式和颜色；信息层及操作提示使用随 DPI 缩放的常规黑色阴影。

主窗体有焦点时，空格切换播放/暂停，方向键跳转 5 秒或调整软件音量，Ctrl+O 打开媒体，S 停止，M 静音，PageUp/PageDown 播放上一项/下一项，R/Shift+R 旋转画面，F11 或 Alt+Enter 切换全屏，Esc 退出全屏；多媒体播放、停止、上一曲/下一曲键也可用。系统音量按键交由 Windows 处理，不会同时修改软件音量。信息层显示时，Ctrl+滚轮优先临时调整信息字号；信息层隐藏、音乐模式显示歌词时，同一操作临时调整歌词字号。字号变化只保留在当前运行状态，不写入设置。歌词以设置中的初始视频渲染区域尺寸为基准，按区域大小分档小幅放大，最大增加 12%，DPI 缩放单独处理。

## 构建

先准备被 Git 忽略的依赖目录：

~~~powershell
.\tools\准备FFmpeg.ps1
.\tools\准备Libass.ps1
.\tools\准备Libiamf.ps1
~~~

LakeUI 是仓库外的同级项目，宿主通过 hint path 引用它。首次构建前先生成：

~~~powershell
dotnet build ..\LakeUI\LakeUI\LakeUI.vbproj -c Debug
~~~

然后运行：

~~~powershell
.\tools\构建3FP.ps1 -Configuration Debug
~~~

脚本会构建 FFF.Native、FFF.Player 和 FFF.Player.Tests。

## 发布与依赖

正式发布可使用 tools/发布3FP单文件.ps1。FFF.Native 和 libass 运行库随单文件释放；FFmpeg Shared DLL 保持外置，由用户提供同一 ABI 的整套 DLL。3FP 自有代码采用 MIT，第三方依赖保留各自许可证和发布义务。

FFF.Player.Tests 保留可编译的测试接入点，不是产品运行时依赖。API 版本和配置结构体大小在构建或发布阶段由项目目标自动校验。
