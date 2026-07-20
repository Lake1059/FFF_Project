## FFF Project

3F 项目 或称 1059 的 3F 帝国，是以 FFmpeg 的 Shared 库制作的软件系列。始祖项目是 [FFmpegFreeUI](https://github.com/Lake1059/FFmpegFreeUI)（3FUI）但由于该产品直接使用 FFmpeg 的最终形态而不包含在本项目内。FFF Project 最大的特色是允许用户自己更换核心，就像 3FUI 一样，这样无需对交互软件进行更改即可享受到最新 FFmpeg 的改进。

要使用本项目中的任何产品，你需要下载 [Shared FFmpeg](https://github.com/BtbN/FFmpeg-Builds/releases/download/latest/ffmpeg-master-latest-win64-gpl-shared.zip) 而不是完全独立的三 EXE 版本，因为这些产品实际上需要的是 `avcodec`、`avformat`、`avutil`、`swresample` 这些 dll 并与产品程序放在同目录，当然更推荐加入环境变量。当然如果有实力的话也可以自己单独编译那些 dll，在 `tools` 目录已经有全自动构建脚本，需要环境变量中的 C++ 编译工具，另外这些 dll 必须配套不得混用，擅自混用导致的任何后果均自行承担。

这些产品的收费政策均和 3FUI 一样：所有生产力功能免费 + 个性化功能收费，无验证无广告。每个产品都需要单独购买，解锁授权不互通，与 3FUI 也不互通，而且价格不低，毕竟咱不能真做慈善来了。


> [!TIP]
> 本项目中的产品均采用单文件发布，需要自行安装 .NET 10 运行库

## FFF.Recorder / 3FR

3FR 是一款简单明确的单源录屏软件，它不对标市面上任何产品，而是一条全新的赛道：3FR 将常规录屏软件的简单快速和近似 OBS 的专业控制进行了融合，可谓是又简单又专业。

- 最低系统要求 Windows 10 1803，推荐 Windows 10 2004+
- 只使用 WGC 进行窗口捕获 + 显示器捕获，反作弊安全
- 收录 AV1/HEVC/AVC 的软件编码器和红绿蓝三家硬件编码器
- 自由使用 CRF/CQ/QP/自定义参数 来进行质量控制
- 支持 CFR/VFR 自由切换
- 支持 420/422/444 采样，支持 8bit/10bit/12bit/16bit
- 支持 HDR PQ，可调节 SDR 亮度和 HDR 最大亮度
- 音频收录 AAC/NMR/FDK，无损支持 WAV 24bit/32bit 和 FLAC
- MKV 直出，无混流合成步骤，被中断仍可正常播放
