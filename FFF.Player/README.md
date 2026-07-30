# FFF.Player / 3FP 非 UI 内核

3FP 的托管入口是 `播放器会话`。调用方必须明确选择 `解码模式.CPU` 或
`解码模式.GPU`，并把用于呈现的 HWND 写入 `播放器配置.输出窗口句柄`，也可以在
窗口创建后调用 `设置输出窗口`。播放器不提供网络协议、设备输入、管道或视频帧服务器。
GPU 模式会依次尝试 FFmpeg 暴露的 CUDA/NVDEC 与 D3D11VA 解码器，并以实际硬件帧验证驱动支持；
AVC/HEVC 的 4:2:0、4:2:2、4:4:4 和 AV1 的 4:2:0 均不在播放器侧按规格预先拒绝，8bit/10bit
同样交给 GPU 与驱动实际判定。硬件后端明确拒绝或在播放中失效时，会自动回退 CPU 解码并继续播放。

静态图片按单帧媒体处理，多帧图片按各帧 PTS 播放；GIF、APNG、Animated WebP 和
Animated JPEG XL 会遵循文件内的循环次数。纯音频中的 `attached_pic` 不作为时间轴
视频流或播放主时钟，而是单独解码为静态封面，由 D3D11 呈现器保持宽高比绘制。媒体可以先在
无窗口模式下打开；播放器会保留已解码的封面帧，并在之后首次绑定有效 HWND 时重新提交，
不要求调用方为了封面改变打开顺序。

色彩输出包含 `映射到SDR`、`原始HDR按SDR呈现` 和 `峰值映射HDR`。请求真实 HDR
但目标显示器或 Windows Advanced Color 不可用时，实际模式自动降级为 SDR，并通过
`色彩模式变化` 事件和 `播放器快照.实际色彩模式` 报告。

SDR 片源在 `映射到SDR` 中保持其 BT.709 码值，不经过 HDR 纸白缩放。HDR→SDR 会先
转换到绝对亮度和 Rec.709，再按帧的 MaxCLL（无此元数据时使用母版峰值或配置回退值）执行
保高光的 Reinhard 压缩；`播放器快照.源峰值尼特` 可用于诊断实际采用的峰值。HDR 的输出模式
只属于当前片源：HDR 后打开 SDR 时，必须先把保留的交换链改回 BGRA/BT.709 并清除 HDR10
元数据，成功后才提交 SDR 状态；重配失败会终止打开，绝不把 SDR 帧送入旧 PQ/BT.2020 链。
默认 100 nit 是 HDR→SDR 的目标峰值，不会乘到纯 SDR 码值上；203 nit 只作为 HDR 漫反射白
映射支点。PQ 按 BT.2100/ST 2084 的绝对亮度解码，HLG 单独按其系统传递函数处理。

色彩处理依据 [ITU-R BT.709](https://www.itu.int/rec/R-REC-BT.709)、
[BT.1886](https://www.itu.int/rec/R-REC-BT.1886)、
[BT.2100](https://www.itu.int/rec/R-REC-BT.2100) 和
[BT.2408](https://www.itu.int/rec/R-REC-BT.2408)。Windows 输出遵循
[Microsoft Advanced Color 交换链契约](https://learn.microsoft.com/en-us/windows/win32/direct3darticles/high-dynamic-range)：
真实 HDR 使用 R10G10B10A2 与显式 PQ/BT.2020 色彩空间；SDR 使用默认桌面合同的
BGRA8/RGB10A2。播放器不读取 ICC 或 Windows SDR 白电平，也不在 shader 中补偿显示设置；
ICC、HDR 校准和 SDR 亮度映射只由 DWM 在窗口合成时应用一次。

`读取视频输出原始像素` 从当前 D3D11 后缓冲复制 1x1 像素，返回 BGRA8、
RGB10A2 的原始通道值。它用于将渲染器数字输出与 ICC、DWM 及屏幕截图
完全分离；窗口化 `Present` 仍必须遵守 Windows 显示色彩契约，后缓冲回读不是物理显示校准。

SDR 视频输出统一使用 D3D11 Video Processor：兼容的 D3D11VA NV12/P010 表面直接进入 VP；
RGB 图片、CPU 解码及需要 HDR→SDR tone mapping 的帧先由 shader 在源分辨率完成格式、色度或色彩处理，
再以 RGB 表面进入 VP。真实 HDR 由 shader 直接写入 10-bit PQ 后缓冲，避开部分驱动在 PQ RGB VP
转换后无法完成 Present 的问题；该路径使用双线性采样保持宽高比缩放。

`播放列表` 负责同目录相似命名扫描、自然排序和本地 M3U8 导入导出；
`播放列表控制器` 可把播放结束事件连接到顺序、循环或随机播放策略。字幕流会出现在
媒体信息中；`TimedText` 目录已经提供与 UI 解耦的 SRT 解析、SUP/PGS 解码、B站 XML 解析、
时间索引、过滤、搜索、相对尺寸缩放、弹幕轨道调度和逐帧绘制指令。ASS/SSA 直接使用 libass
的局部 Alpha 遮罩，不再经过 FFmpeg 全画布滤镜，也不回退简化文字解析。
UI 呈现器提交逐帧命令，Native D3D11/DirectWrite 层负责栅格化和合成，详见 `TimedText/README.md`。

性能相关资源有明确所有权：FFmpeg packet、解码 frame、硬件回读 frame 和最多 8 帧的视频队列
外壳只归播放工作线程复用。WASAPI 以解码样本数维护连续 PCM 时间线：首帧、Seek 和真实的
100 ms 以上断点才使用 PTS 锚定或修复，普通 VBR/AAC 时间基量化只计入诊断，禁止逐帧补静音
或裁样。播放位置取自 `IAudioClock`，而不是根据两次 padding 差值猜测；设备事件之间用
`IAudioClock` 关联的 QPC 采样点连续外推，但绝不越过已经提交给 WASAPI 的 PCM 末端。这样既
保持硬件时钟权威，也为 60 Hz 以上的文字运动提供连续媒体时间。采样率、样本格式或完整声道
布局变化会重建重采样器。外部音轨启用期间主容器音频包会被跳过，因此清除外部音轨
必须对主容器执行完整 Seek，同时复位视频、音频、重采样器和时钟，不能只关闭外部解码器。
应用 PCM 与已经提交给设备的 PCM 合计以 120 ms 为目标；共享模式按设备周期申请 30–50 ms
端点缓冲。回归合同要求稳定播放时平均缓冲不超过 200 ms、峰值不超过 300 ms，并且不能欠载。
共享与独占模式使用分离的控制事件和设备事件；独占设备每次回调只写一个完整端点周期。
独占状态下打开另一媒体时，控制器先把旧会话临时切回共享以释放端点；候选媒体打开成功后
释放旧会话，并把新会话恢复到用户选择的独占模式。候选打开失败或取消时则恢复旧会话独占，
因此调用方不需要先手工切回共享模式。
信息页响度直接统计实际提交给 WASAPI 的每声道 PCM 峰值，不依赖系统混音器的
`IAudioMeterInformation`，所以独占模式绕过混音器后仍能持续取得响度。

定时文字 P/Invoke 的 UTF-8 指针和位图固定地址只在同步调用期间有效，Native 必须在返回前保留；
托管侧容量和 UTF-8 指针缓存均有界。稳定文字按 `内容标识+UTF-8` 驻留为共享不可变内容，并一次
栅格化为带安全边距的 GPU 精灵；滚动帧只做线性采样平移，不得重复栅格化同一字形。字幕和
弹幕各自拥有托管定时泵、提交序号、Native 图层槽位和透明 GPU 纹理，弹幕目标帧率不依赖
视频或字幕帧率，可独立扩展到 90/120/144/240 Hz。两个生产者只发布最新状态，唯一的 Native
呈现线程按目标帧率合并更新并独占交换链 `Present`；不得让视频线程或任一图层线程直接提交
交换链，否则完整合成帧会在翻转队列中交替而产生闪烁。最终顺序固定为视频、弹幕、字幕，
因此字幕始终覆盖弹幕。D3D11 flip-model 的逻辑缓冲 0 在每次 `Present` 后可能对应不同物理
缓冲，必须逐帧重新 `GetBuffer(0)` 并创建当帧 RTV；禁止跨呈现周期缓存这两个对象，否则两个
物理缓冲会交替出现有/无文字图层。最终合成还是完整的 D3D 管线边界，必须显式恢复全屏
顶点着色器、拓扑、视口、常量缓冲、采样器和混合状态；不得继承弹幕实例精灵 Pass 留下的
管线状态。定时泵不依赖 UI 消息队列。`内容标识`
必须随文字内容和样式稳定变化，
对象池中的命令在归还前必须清空所有可变字段。外部字幕替换通过使用租约延迟释放 SUP 解码器，
保证后台图层生成与原子换轨不会并发访问已释放资源。播放中打开或拖入 SRT/ASS/SSA/SUP
会原子替换当前字幕，打开或拖入 B 站 XML 会原子替换当前弹幕；两者都必须先在后台完整解析，
成功后才发布新资料，不得清空旧图层、重建媒体会话或改变播放位置及音视频流选择。

UI 应把自己的 `SynchronizationContext` 写入 `播放器配置.事件同步上下文`，所有低频
事件随后会按顺序投递到该上下文；未提供时，事件会在独立的托管线程池队列中串行触发，
不会占用 Native 播放线程。`打开Async` 支持取消，取消后会排队关闭正在打开的媒体；
释放会话后，已经投递但尚未执行的事件不会再触发。

播放、暂停、停止、跳转和切流会在调用时校验当前状态，不适用的命令会抛出
`播放器异常`，不会静默忽略。UI 可高频读取 `当前快照`，该操作不会访问或阻塞 WASAPI
设备对象。3FP API v6 的快照另外提供 `已解码音频帧数`、`音频位置`、`音频缓冲时长` 和
`音频欠载次数`、PTS 抖动帧数、真实断点数、补零和裁样帧数，这些字段只用于诊断。窗口 resize 或移动显示器后，
再次调用 `设置输出窗口`（允许传入同一个 HWND）会复用 flip-model 交换链并触发重绘；呈现时
按需 Resize 或重配色彩空间，播放状态和位置不会改变。同一 HWND 不得并存两个 flip-model 链。

内部回归测试不采集画面。先构建 Release x64 的 `FFF.Native` 和 `FFF.Player.Tests`，再运行：

```text
FFF.Player.Tests --audio-latency-regression
FFF.Player.Tests --audio-cover-regression <带内嵌封面的纯音频>
FFF.Player.Tests --color-regression <SDR视频> <HDR视频>
FFF.Player.Tests --hdr-switch-regression <HDR视频>
FFF.Player.Tests --performance-regression <SDR视频> <HDR视频>
FFF.Player.Tests --targeted-regression <视频> <字幕.sup>
FFF.Player.Tests --vcb-ass-regression <视频> <字幕.ass>
FFF.Player.Tests --gpu-decode-matrix <视频目录>
FFF.Player.Tests --video-scaling-regression <视频>
FFF.Player.Tests --sdr-pixel-regression <视频> <参考图.png>
FFF.Player.Tests --ass-render-benchmark
FFF.Player.Tests --timed-text-regression
```

音频延迟回归自生成双声道 PCM，在完全无画面条件下覆盖共享/独占时钟、缓冲、欠载和每声道响度；
封面回归先无窗口打开纯音频，再绑定一个不显示的 HWND，只检查封面流、尺寸和交换链呈现计数。
色彩回归覆盖 SDR 码值直通、PQ 数值映射和 HDR→SDR 换片；HDR 切换回归在真实窗口中验证
播放期间的 SDR→10-bit PQ→SDR 交换链切换、视频出帧和文字图层持续合成；性能回归固定覆盖 CPU 解码、呈现、
独立字幕层/100 条同时移动弹幕层、至少 55 FPS 的弹幕合同、音频缓冲、外部音轨偏移、Seek 和
恢复内置音轨。专项回归验证连续 AAC PCM 在开头和 1000 秒 Seek 后都不会误补零/裁样，
并验证 SUP/SRT/ASS/SSA 字幕与 XML 弹幕的播放中原子替换及损坏文件回退。

`tools/构建3FP.ps1` 优先使用环境中的工具，其次使用正式版 Visual Studio，最后回退到预览版，
并准备 libass、构建 Native、Player 和测试项目；
FFmpeg 依赖由 `tools/准备FFmpeg.ps1` 固定到同一 commit。运行时需要 `avcodec`、`avformat`、`avutil`、
`swresample`、`swscale`、`avfilter`、`FFF.Native` 以及 libass。正式发布可使用
`tools/发布3FP单文件.ps1`：FFF.Native 和 libass 运行库会进入单文件，FFmpeg DLL 仍保持外置，
用户可把同一套兼容 ABI 的 Shared FFmpeg DLL 放在程序目录或可搜索路径中并整体替换版本。
