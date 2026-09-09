## FFF Project

## 许可证与发布约束

整个 3F 项目的自有源码以 MIT 许可证发布，见 `LICENSE.txt`。第三方库保留各自的
许可证；项目的 MIT 声明不改变第三方代码或组合发行物所需履行的义务。

3FP 的正式发行物不包含 FFmpeg：`avcodec`、`avformat`、`avutil`、`swresample`、
`swscale`、`avfilter` 等兼容 ABI 的 Shared DLL 由用户自行提供，必须整套使用。
开发构建可以复制本机 FFmpeg DLL 供测试，但不得把它们打包进正式发行 EXE 或发布目录。
不要求用户提供的 FFmpeg 必须是 LGPL 构建。

`FFF.Native`、libass 及其他随产品分发的原生依赖，由发行脚本嵌入单文件 EXE，
通过 .NET 的原生库自解压机制在运行时释放到缓存目录。新增依赖遵循同一布置方式，
同时保留版本、来源与许可证记录。GPL 依赖不能因采用 DLL、自解压或 MIT 主项目而
视为 MIT 组件；含此类依赖的组合发行需要满足其许可证要求。

3F 项目 或称 1059 的 3F 帝国，是以 FFmpeg 的 Shared 库制作的软件系列。始祖项目是 [FFmpegFreeUI](https://github.com/Lake1059/FFmpegFreeUI)（3FUI）但由于该产品直接使用 FFmpeg 的最终形态而不包含在本项目内。FFF Project 最大的特色是允许用户自己更换核心，就像 3FUI 一样，这样无需对交互软件进行更改即可享受到最新 FFmpeg 的改进。

要使用本项目中的任何产品，你需要下载 [Shared FFmpeg](https://github.com/BtbN/FFmpeg-Builds/releases/download/latest/ffmpeg-master-latest-win64-gpl-shared.zip) 而不是完全独立的三 EXE 版本，因为这些产品实际上需要的是 `avcodec` 等那一堆 dll 而不是 ffmpeg 的 exe，并与产品程序放在同目录，当然更推荐加入环境变量。如果有实力的话也可以自己单独编译那些 dll，在 `tools` 目录已经有全自动构建脚本，需要环境变量中的 C++ 编译工具，另外这些 dll 必须配套不得混用，擅自混用导致的任何后果均自行承担。

这些产品的收费政策均和 3FUI 一样：所有生产力功能免费 + 个性化功能收费，无验证无广告。每个产品都需要单独购买，解锁授权不互通，与 3FUI 也不互通，而且价格不低，毕竟咱不能真做慈善来了。


> [!TIP]
> 本项目中的产品均采用不含运行时的单文件发布<br>需要自行安装 .NET 10 运行库

## FFF.Recorder / 3FR

3FR 是一款简单明确的单源录屏软件，它不对标市面上任何产品，而是一条全新的赛道：3FR 将常规录屏软件的简单快速和近似 OBS 的专业控制进行了融合，可谓是又简单又专业。

- 最低系统要求 Windows 10 1803，推荐 Windows 10 2004+
- 只使用 WGC 进行窗口捕获 + 显示器捕获，反作弊安全
- 收录 AV1/HEVC/AVC 的软件编码器和红绿蓝三家硬件编码器
- 自由使用 CRF/CQ/QP/自定义参数 来进行质量控制
- 支持 CFR/VFR 自由切换
- 支持 420/422/444 采样，支持 8bit/10bit
- 支持 HDR PQ，可调节 SDR 亮度和 HDR 最大亮度
- 音频收录 AAC/NMR/FDK，无损支持 WAV 24bit/32bit 和 FLAC
- MKV 直出，无混流合成步骤，被中断仍可正常播放

## FFF.Player / 3FP

3FP 是一款独特路线的本地视频播放器，也支持单独的音频和图片。自主 D3D11 渲染引擎，由 avcodec 等 dll 解码后直接送到自己的渲染逻辑中，没有任何第三方插手，全 GPU 渲染链路 + 真实高亮 HDR 支持，占用极低，功能极简，可完全取代普通人的日常本地播放需求。

私有协议：3FP 作为 3FUI 的全新可视化剪辑区间交互软件，当 3FP 切换至剪辑区间模式时键盘方向键变为帧级精确操作以及提供帧级时间戳显示，可一键送入 3FUI 的参数面板。

- 过老的 Win10 版本不兼容，强烈推荐 Windows 10 22H2+
- 界面仿 PotPlayer 设计，更易于上手，但功能有较大差别
- 共享式打开文件，不占用文件，播放中仍可移动和重命名
- 理论上支持 ffmpeg 的全部编码，工程上需要逐步适配
- 采用 WASAPI 音频输出，支持独占模式以提供最佳音质
- 支持 SRT / ASS / SSA / SUP 字幕
- 支持 LRC 歌词，还有音乐封面 + 毛玻璃背景
- 支持哔哩哔哩规范的弹幕
- 三挡 HDR 模式，适配不同的屏幕和场景
  - 映射 SDR：映射到 SDR 色域，适合普通屏幕观看
  - 原始 HDR 灰：仍是 SDR 所以呈现发灰，特殊需求
  - 真实 HDR 高亮：按源 HDR 规格映射到实际亮度范围，需要 HDR 屏幕
- HDR 多规格支持
  - HDR10、HDR10+、HLG
  - Dolby Vision（不提供实际支持，强制映射 HDR10）
  - HDR Vivid
