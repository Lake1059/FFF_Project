## FFF Project

3F 项目 或称 1059 的 3F 帝国，是以 FFmpeg 的 Shared 库制作的软件系列。始祖项目是 [FFmpegFreeUI](https://github.com/Lake1059/FFmpegFreeUI)（3FUI）但由于该产品直接使用 FFmpeg 的最终形态而不包含在本项目内。FFF Project 最大的特色是允许用户自己更换核心，就像 3FUI 一样，这样无需对交互软件进行更改即可享受到最新 FFmpeg 的改进。

要使用本项目中的任何产品，你需要下载 [Shared FFmpeg](https://github.com/BtbN/FFmpeg-Builds/releases/download/latest/ffmpeg-master-latest-win64-gpl-shared.zip) 而不是完全独立的三 EXE 版本，因为这些产品实际上需要的是 `avcodec`、`avformat`、`avutil`、`swresample` 这些 dll 并与产品程序放在同目录。当然如果有实力的话也可以自己单独编译那些 dll，在 `tools` 目录已经有全自动构建脚本。不过需要注意的是这些 dll 必须配套，不得混用，擅自混用导致的任何后果均自行承担。

这些产品的收费政策均和 3FUI 一样：所有生产力功能免费 + 个性化功能收费，且每个产品都需要单独购买，解锁授权不互通，与 3FUI 也不互通，而且价格不低，毕竟咱不能真做慈善来了。

## FFF.Recorder

3FR 是一款简单明确的单源录屏软件，它不对标市面上任何产品，而是一条全新的赛道：3FR 将常规录屏软件的简单快速和 OBS 级别的专业控制进行了融合，可谓是又简单又专业。

- 窗口捕获 + 显示器捕获
- 理论兼容所有游戏和反作弊系统
- 支持 HDR 内容
- 支持 4:4:4 采样
- 与 OBS 相仿的质量控制
- 高质量音频
