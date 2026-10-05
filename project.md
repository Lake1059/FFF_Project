# 3FP 内核性能候选

## 边界

- 基线：Lake1059/FFF_Project `58818728d4aeccf68b7fb9662155b6614d1c569b`（2026.10.5）。
- 工作分支：`codex/player-performance`；人工测试已通过，提交上游 PR 供审查，不发布正式 Release。
- 不改 UI、托管播放器、公共 API、版本号、缩放算法、色彩算法、字幕或音频逻辑。
- 本地 VS Player 的 Jinc/LUT、VideoProcessor 路径、VS 接口、Qt 功能不迁入；第二轮只补齐其三缓冲和 Present 调度。
- 上游已经包含呈现背压、有界队列、定时文字精灵缓存，不重复修改。

## 三项内核改动

1. CPU planar YUV 使用 D3D11 DYNAMIC / WRITE_DISCARD 上传三平面，允许驱动重命名忙碌的纹理存储。逐行复制遵守 FFmpeg stride、色度尺寸和 8/16 位存储；硬解表面、NV12/P010、RGB 上传路径保持原版。
2. 现有 PixelShaderSource 生成三个 SDR/Rec.709 格式专用的预编译着色器。仅固定已知的 InputLayout、ColorMode、Transfer、Gamut、Reserved、Projection360，让编译器删除分支；Lanczos3、抗振铃、YUV 矩阵及上下游多级 Hermite/Lanczos3 缩放公式均保持。HDR、广色域、360、封面和扩展路径仍走原着色器。专用 shader 创建失败则使用原版 shader。
3. 软件解码器确为 libdav1d 且分辨率至少 3840×2160 时，线程上限 8→16，仍受硬件并发数限制。其他编码/分辨率、硬解、光盘菜单策略不变。可能增加高分辨率 AV1 解码器内存，需要低性能设备测试后决定是否保留。

## 构建

需要 Visual Studio C++ v145、Windows SDK、.NET 10 SDK、Shared FFmpeg 63/63/61/12/7/10 配套开发文件和运行库，以及上游要求的 LakeUI/libass/libbluray。遵循上游构建脚本；不为本分支修改 UI 项目依赖配置。

```powershell
python tools/generate_shader_bytecode.py --sdr-specializations
tools/构建3FP.ps1 -Configuration Release
```

改动原 HLSL 时还需按原流程运行不带选项的生成器，更新原版 ShaderBytecode.h；本候选没有重写原版字节码。已校验原版主 shader 数组与当前源码用 Windows SDK fxc /O3 重新编译的字节码一致。

本机 vcpkg/libiconv 的 Autotools 配置在含空格构建目录失败，因此只将临时依赖构建目录放在无空格路径，再使用相同产物构建两版；上游依赖脚本不改。

## 验证与交付

`tools/kernel-probe.cpp` 通过未改动的 Native ABI 加载指定 DLL，建立独立窗口，不读写播放器配置或输入媒体。可检查首帧、非连续 SeekFrame 上传、播放计数与输出像素。

```text
kernel-probe DLL VIDEO WIDTH HEIGHT DECODE QUALITY COLOR PROJECTION SECONDS PIXELS
```

DECODE：1 CPU、2 GPU；QUALITY：0 均衡、1 高质量；COLOR：0 SDR映射、1 原始HDR按SDR、2 HDR；PROJECTION：0 普通、1 360；SECONDS=0 检查首帧与跳转并输出确定性帧像素，正值进行指定秒数播放。

原版和优化版必须使用相同托管程序、FFmpeg、第三方库及编译配置，只有 FFF.Native.dll 不同。测试包用于本机/用户设备人工验证，不是正式上游发行；第三方许可证义务不变。

## 本机验证结果（2026-10-05）

- 原版/优化版 Native x64 Release 与未修改的托管 UI 构建、self-contained 文件夹发布成功。
- 18 组像素回归（CPU/GPU、420/422/444、8/10/16 位存储、放大/缩小、两种缩放质量、PQ/HLG、原始 HDR 按 SDR、360）完成。16 组完整 RGBA float 回读字节一致；10-bit 444 均衡放大和 GPU P010 两组各有一个通道相差一个 10-bit 量化级，最大约 0.00097753。这是允许编译器精简分支后的舍入差异，不声称全部零差异；测试限制差异不能超过输出的一个量化级。
- 上述每组执行帧 0→5→0 非连续定位，再读像素；GPU NV12/P010 两组确认实际 decode=2，未回退软件。
- 带 AAC 音频的 1080p60：CPU、GPU，各版本播放 8 秒通过，dropped=0、coalesced=0。
- 4K60 AV1：CPU，各版本播放 8 秒通过，dropped=0、coalesced=0。
- 本机 NVIDIA 0x2860，性能测试窗口 960×540；这里只是功能冒烟，不能代表低性能设备、4K 全屏或长时间稳定性，不用于宣称帧率提升。
- 两版发布文件逐一 SHA256 对照，唯一差异为 FFF.Native.dll；上游基线工作树干净，公共 API 与 UI 源码没有改动。
- 打包补齐增强 FFmpeg 的外部 DLL、VC145 app-local CRT 和 .NET 10.0.11。仅 Windows/System32 PATH 环境，两版加载/带音频 GPU 播放通过。包内 FFmpeg 为本机已有的 x86-64-v3/AVX2 构建；内核没有新增指令集要求，较老设备需为两版同时换用兼容的 Shared FFmpeg。
- 优化版 EXE 隐藏启动 3 秒未退出；未进行可视化 UI 验收，人工交互验收仍由用户完成。
- 测试探针最初错误地等待 Ready 自动输出视频；上游仅图片在 Open 时主动解首帧。修正探针为 Ready 后显式 SeekFrame(0)，生产代码的打开/播放行为未改。

人工测试：两目录各自启动 FFF.Player.exe，同一片源、同一 CPU/GPU 设置、缩放质量、色彩模式及窗口尺寸，对照全屏稳定播放、跳转、暂停恢复、音频与字幕。重点观察弱 GPU 的大窗口 SDR 呈现，以及超过 8 个逻辑线程的设备上高分辨率 AV1 的速度/内存。需测试 4K/8K、HDR/广色域、360、字幕弹幕和多次换片，确认没有回归再提 PR。

不要把着色器字节码变小或线程变多当作已证明的帧率提升。用户设备人工测试结果见下文；其他设备和素材仍需验证。

## 第二轮：D3D11 调度完整性（2026-10-05）

用户反馈第一轮在其 8K48 素材仅 30–35 fps、提升很小。对照本地 VSP 确认：第一轮并非完整移植 D3D11 原生路径。VSP 的主要收益包括 VideoProcessor 直接消费解码数组 slice（去掉整帧复制）、三缓冲/两帧排队、阻塞 Present 时释放上下文锁，以及稳定链上传免等 Present。

用户再次明确选择“保持上游算法，只优化内核调度”。因此 VideoProcessor、数组 slice 直呈和双线性/驱动缩放仍不启用，原版 Shader 的复制、色彩和多级缩放管线继续保留。不能把本分支标为 VSP D3D11 直呈的完整等价移植，也不能保证相同 8K48 吞吐。

此次只新增：

1. flip-discard 缓冲数 2→3，以 IDXGIDevice1::SetMaximumFrameLatency(2) 约束排队，保持原来的垂直同步策略。原交换链没有 FRAME_LATENCY_WAITABLE_OBJECT 标志，调用交换链的 SetMaximumFrameLatency(1) 无效。增加并行缓冲的代价是额外一个后缓冲及最多两帧的排队延迟。
2. PresentTimedText 完成合成后保留交换链/后缓冲引用，保持 presentMutex_ 防止销毁/重配，但在阻塞 Present 时释放 deviceMutex_；释放全部旧缓冲引用并解开 presentMutex_ 后才重新获取 deviceMutex_，避免锁顺序死锁。
3. 仅稳定 SDR 链（窗口物理尺寸、输出精度、色彩模式都匹配）上传帧时不再获取 presentMutex_。创建、resize、HDR、色彩重配依旧使用原来的双锁和原算法。

依据：[交换链延迟接口要求等待对象标志](https://learn.microsoft.com/en-us/windows/win32/api/dxgi1_3/nf-dxgi1_3-idxgiswapchain2-setmaximumframelatency)、[设备帧队列接口](https://learn.microsoft.com/en-us/windows/win32/api/dxgi/nf-dxgi-idxgidevice1-setmaximumframelatency)。

验证：Native Release 重编译成功；18 组回归与第一轮保持同样结果（16 组完全一致，2 组各一个通道有一个 10-bit 量化级差异）。本机合成 7680×4320、AV1 420 8-bit、48 fps→3840×2160、高质量 Shader，第一/第二轮各播放 8 秒，均 384 解码、385 接受、零 dropped/coalesced，位置推进 8.0008 秒；本机显卡未形成第一轮瓶颈，因此不据此宣称性能提升。

8 秒并发压力两组：带音频的 1080p60 GPU 和 PQ 素材 CPU，约每秒四次 resize/还原窗口、切换色彩模式（包括强制 HDR）、添加/清空字幕，通过，无死锁、设备失败、丢帧或音频欠载。测试工具增加可选 stress 参数以及 Present/设备锁等待统计；不修改生产 API。未进行用户低性能设备真实素材测试。

本轮交付只含优化版，不重打包基线。

## 人工验收与上游提交（2026-10-05）

用户反馈：780M、3FP HW 模式通过真实素材 8K48 测试，跳帧时的主观流畅性甚至优于 VSP。该结果由用户人工测试提供，不包含独立采集的帧时间、丢帧计数或跨设备统计，不外推为所有设备的性能保证。

用户确认开发者的色彩更新已在前一晚完成，本候选测试基线已经包含该更新。提交前抓取上游，新增的 `0f4a731` 仅调整发布脚本，Native 渲染代码没有变化；将本候选移到该最新基线，不引入额外色彩、缩放或 UI 修改。用户已授权提交 PR；不创建正式 Release 或自动跟进任务。
