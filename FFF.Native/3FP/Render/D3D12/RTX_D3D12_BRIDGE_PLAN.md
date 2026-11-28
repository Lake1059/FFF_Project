# D3D12 RTX VSR / RTX Video HDR 桥接实施计划

## 1. 目标

在保留 3FP 的 D3D12VA 解码与 D3D12 呈现路径的前提下，让 NVIDIA RTX VSR 与 RTX Video HDR 在 D3D12 后端真实生效，并由实际 `VideoProcessorBlt` 结果回填状态，禁止在 D3D12 后端伪造 `active`。

验收以真实运行状态为准：

- VSR：D3D12、720p H.264 8-bit、1920x1080 窗口，`requested=1 nv=1 active=1 rejected=0 reason=0`，源/目标尺寸非零。
- HDR：在真实 Windows HDR 输出路径上，8-bit SDR 源执行 `requested=1 nv=1 active=1 rejected=0 reason=0`，`actualColor=MapToHdr`、`bitDepth=16`；不使用 `FFF_PROBE_FORCE_HDR`。

## 2. 现状与约束

已实测确认：

- D3D12 路径在 `PlayerVideoRenderer::BuildD3D12Picture` 中无条件把 VSR/HDR 置 inactive，并对非 Off 请求写 `UnsupportedRenderBackend`。
- D3D12 没有创建 `ID3D11VideoProcessor`；VSR/TrueHDR 依赖 `ID3D11VideoContext::VideoProcessorSetStreamExtension / GetStreamExtension`。
- 独立 D3D11 设备可以创建 `ID3D11VideoDevice`、`ID3D11VideoContext`、`ID3D11VideoProcessor`，TrueHDR `GetStreamExtension` 返回 supported，VSR/TrueHDR Set 返回 `S_OK`。
- `D3D11On12CreateDevice` 也能创建视频处理器，但 TrueHDR/VSR 私有扩展返回 `E_NOTIMPL`，因此不能复用现有 D3D12 文本设备的 D3D11On12 实例。
- `D3D12VideoRenderer::Upload` 已经把 D3D12VA 解码表面拷贝到自有的 `decodedTexture_`（NV12/P010 两平面），桥接可直接复用这块表面，不需要再增加一次输入拷贝。
- D3D11 VSR 输出与源同格式；TrueHDR 输出 `DXGI_FORMAT_R10G10B10A2_UNORM`；TrueHDR 可以同时携带 VSR PPE，一次 `VideoProcessorBlt` 完成两者。

实现范围仅限 `3fp\kernel`。3FC 外层继续遵守暂停规范，不修改。

## 3. 架构

新增 `FFF.Native/3FP/Render/D3D12/NvidiaD3D12VideoBridge.h/.cpp`。

### 3.1 设备与资源

- 桥接在使用时创建独立 D3D11 设备，设备与 D3D12 设备位于同一 NVIDIA adapter。
- `D3D12VideoRenderer::decodedTexture_` 改为 shared NT handle：
  - `D3D12_HEAP_FLAG_SHARED`
  - `CreateSharedHandle(..., GENERIC_ALL, ...)`
  - D3D11 用 `OpenSharedResource1` 打开为 NV12/P010 输入纹理。
- 桥接输出环 2–3 个 shared 纹理：
  - VSR-only：与源同格式（NV12/P010），目标尺寸为 `picture.viewport` 在源轴上的偶数化尺寸。
  - TrueHDR：`DXGI_FORMAT_R10G10B10A2_UNORM`。
  - D3D11 创建时加 `D3D11_RESOURCE_MISC_SHARED_NTHANDLE`，`CreateSharedHandle` 给 D3D12 `OpenSharedHandle`。
- D3D12 采样输出前，资源状态从 `COMMON` 转到 `PIXEL_SHADER_RESOURCE`；绘制完成后转回 `COMMON`，供下一帧 D3D11 写入。

### 3.2 同步（零 CPU Wait）

三根 shared fence，全部由 D3D12 创建、D3D11 打开：

1. `uploadFence`：D3D12 完成 decoded 拷贝并 transition 到 `COMMON` 后 signal；D3D11 `Process` 先 Wait。
2. `readyFence`：D3D11 `VideoProcessorBlt` 完成后 signal；D3D12 在 RecordDraw 前 `queue_->Wait`。
3. `consumedFence`：D3D12 完成包含桥接输出的绘制后 signal；D3D11 复用输出环槽位前 Wait。

命令流：

```
D3D12 Upload: copy decode -> decodedTexture, transition COMMON
              signal uploadFence

D3D11 Bridge: Wait(uploadFence)
              Wait(outputSlot consumedFence)
              VideoProcessorBlt
              signal readyFence

D3D12 Draw:   Wait(readyFence)
              transition output COMMON -> PIXEL_SHADER_RESOURCE
              draw / scale / overlays
              transition output -> COMMON
              signal consumedFence
```

任何一段失败都不得把 VSR/HDR 标成 active；失败原因进入状态。

### 3.3 处理路径

`NvidiaD3D12VideoBridge` 负责：

- 校验源/目标门。
- 创建/缓存 `ID3D11VideoProcessorEnumerator`、`ID3D11VideoProcessor`、输入/输出 view。
- 配置 `D3D11_VIDEO_PROCESSOR_STREAM`、源/目标矩形、帧格式、背景色。
- 设置 VSR/TrueHDR 私有 stream extension。
- 一次 `VideoProcessorBlt`。
- 返回本次输出纹理槽位、ready fence value、实际 active 状态与诊断。

门条件与 D3D11 路径保持一致：

- `sourceInputLayout == 2`，NV12/P010，bit depth ≤ 10。
- 非交错、非 cover art、非 360 projection、chroma location 为 LEFT/UNSPECIFIED。
- 源高 360–1440，目标确实为放大，目标宽高偶数化。
- TrueHDR 额外要求：SDR transfer、`actualMode == MapToHdr`、`swapHdr == true`。
- TrueHDR 优先于 VSR；两者同时请求时，TrueHDR pass 携带 VSR PPE，VSR 状态按现有语义写 `YieldedToAutoHdr`。
- VSR-only 输出 YUV，继续走原有色彩管线；TrueHDR 输出 RGB10，`Picture` 做与 D3D11 相同的重标记：`inputLayout=0`、`sampleScale=1.0`、`transfer=1`、`gamut=1`、`hdrPeak=1000`。

## 4. 渲染器接线

### 4.1 `BuildD3D12Picture`

- 删除无条件 inactive + `UnsupportedRenderBackend`。
- 把 VSR/HDR 请求、源格式/位深/transfer、`actualMode`、`swapHdr`、目标 viewport 传给 `D3D12VideoRenderer`。
- 未请求时状态仍为 `NotRequested`；请求后由桥接实际结果回填。

### 4.2 `D3D12VideoRenderer::Upload`

- `decodedTexture_` 共享化。
- 拷贝完成后 transition 到 `COMMON`。
- 若桥接对象存在，signal `uploadFence`，记录 value。

### 4.3 `D3D12VideoRenderer::RecordDraw`

- 桥接请求有效且 `Process` 成功时，跳过 `decodedTexture_` 直接采样分支：
  - VSR-only：使用桥接 YUV 输出的两平面 View。
  - TrueHDR：使用桥接 RGB10 输出的单平面 View，并按 TrueHDR 规则修改局部 `Picture`。
- 跳过 D3D12 shader scale chain；输出已经是目标尺寸。
- 绘制前后做 `COMMON ↔ PIXEL_SHADER_RESOURCE` 转换。
- 记录本次输出槽位，供 `Submit` / `ReadPicture` 之后 signal `consumedFence`。

### 4.4 状态上报

- `Submit` / `ReadPicture` 成功后读取桥接状态，更新：
  - `videoSuperResolutionActive_`
  - `videoSuperResolutionReason_`
  - 源/目标尺寸
  - `videoTrueHdrActive_`
  - `videoTrueHdrReason_`
  - 源/目标尺寸、输出尺寸
- `SubmitsD3D12Frame` 的 `actualVideoScalingMode_` 在桥接生效时写 `D3D11VideoProcessor`，否则保持 `Shader`。
- `SetVideoSuperResolution(Auto)` / `SetVideoAutoHdr(Auto)` 重置桥接的失败锁存，允许重新探测。

## 5. 分阶段实施

### 阶段 A：桥接骨架与共享资源

1. 新增桥接头/源文件，接入 vcxproj。
2. 实现 shared decoded 纹理创建与 D3D11 打开。
3. 实现输出环、shared handle、shared fence。
4. 实现 VSR-only 与 TrueHDR 两条 blit 路径。
5. 先独立编译，确保 D3D11/D3D12 头与链接正确。

### 阶段 B：渲染器接线

1. `Upload` 共享化与 uploadFence。
2. `RecordDraw` 桥接分支、状态转换、输出 View。
3. `Submit` / `ReadPicture` consumedFence。
4. `BuildD3D12Picture` 请求传递与状态回填。
5. 日志：只保留失败/切换/首帧，避免每帧刷屏。

### 阶段 C：构建

1. 用 VS 18 `vcvars64.bat` + MSBuild 构建 `FFF.Native.vcxproj` Release x64。
2. 处理所有编译错误与链接错误。
3. 跑最小 D3D12 播放，确认不黑屏、不 TDR。

### 阶段 D：实机验证

VSR：

```
seek_skip_probe.exe C:\Temp\seek_skip_h264_720p.mp4 gpu12 exact 4.5 skip0
FFF_PROBE_FEATURES=1
```

期望：`VSR active=1 reason=0 src=1280x720 tgt≈1852x1042`，`backend=1`、`decodeMode=2`。

HDR：

1. 在 Windows HDR 已启用 + HDR10 显示器上验证 8-bit SDR 源。
2. 使用 `FFF_PROBE_HDR_MODE=1`，但**不加** `FFF_PROBE_FORCE_HDR`，确认 3FP 依据真实显示能力进入 scRGB 链，期望 `actualColor=2`、`bitDepth=16`、`HDR active=1 reason=0`。
3. 若 swap chain 拒绝 scRGB，则如实报告 `OutputPathNotHdr`，并保留底层 D3D11 TrueHDR 探针结果，不伪造 active。

回归：

- D3D12 普通 SDR 播放、seek、4K、10-bit、交错、多路窗口。
- `run_seek_skip_tests.py --d3d12`。
- SecurityRegression（若改动触及 API/资源生命周期）。

## 6. 验收标准

- VSR 在 D3D12 下 `active=1`，且确实跳过 D3D12 shader scale chain（`actualVideoScalingMode=D3D11VideoProcessor`）。
- HDR 在可用 HDR 输出路径下 `active=1`，输出 `R10G10B10A2` 并经 TrueHDR 重标记呈现；不得以“驱动支持”代替 `active=1`。
- 桥接失败时状态必须是真实原因，不得回退为假 active。
- 普通 D3D12 播放与既有 seek/security 回归不退化。
- 无每帧 CPU busy-wait；稳态渲染线程不做 `WaitForSingleObject` 等 GPU 完成。

## 7. 风险与回退

- **多会话**：每适配器共享一个 D3D11 设备可减少设备数，但 D3D11 immediate context 会串行化；首版按每 D3D12 renderer 独立桥接，9 路并发实测后再决定是否池化。
- **跨 API 状态**：shared 资源必须在 `COMMON` 与 D3D12 采样状态之间正确切换；错误状态会黑屏或设备移除。
- **格式门**：P010、奇数尺寸、旋转、HDR 输出链可能触发驱动拒绝；全部按真实 HRESULT 上报。
- **HDR 面板**：本机实际是 HDR（DisplayConfig `supported=1 enabled=1`、10 bpc；DXGI `RGB_FULL_G2084_NONE_P2020`，峰值 1015 nits），FP16 swap chain 接受 scRGB。HDR 验证不依赖强制输出；若其他机器为 SDR 面板，仍按真实能力回退并写原因。
- **回退**：桥接初始化或首帧失败时，释放桥接资源并回退到原 D3D12 shader scale；请求状态写真实失败原因，不影响普通播放。

## 8. 涉及文件

- 新增：`FFF.Native/3FP/Render/D3D12/NvidiaD3D12VideoBridge.h`
- 新增：`FFF.Native/3FP/Render/D3D12/NvidiaD3D12VideoBridge.cpp`
- 修改：`FFF.Native/3FP/Render/D3D12/D3D12VideoRenderer.h/.cpp`
- 修改：`FFF.Native/3FP/Render/VideoRenderer.cpp`
- 修改：`FFF.Native/FFF.Native.vcxproj`
- 测试：`FFF.Native.Tests/` 下新增或扩展现有真实窗口探针

## 9. 实施与实机结果（2026-11-28）

实现已完成，构建与真实窗口验证结果如下。验证媒体与日志位于 `C:\Temp`，不进入仓库。

### 9.1 构建

`FFF.Native/FFF.Native.vcxproj` Release x64 构建成功，生成新 `FFF.Native.dll` 与 PDB。构建日志：`C:\Temp\build_3fp_rtx_hdrfix.log`。

### 9.2 VSR（D3D12 真实窗口）

命令：

```
FFF_PROBE_FEATURES=1
seek_skip_probe.exe C:\Temp\seek_skip_h264_720p.mp4 gpu12 exact 4.5 skip0
```

实测结果：

```
FEATURE vsr req=1 nv=1 active=1 rejected=0 reason=0 src=1280x720 tgt=1852x1042 |
        hdr req=1 nv=1 active=0 rejected=0 reason=5 ... | actualColor=0 bitDepth=8 scale=1
mode=gpu decodeMode=2 action=exact ... uploadPath=10 backend=1
```

对应日志：`C:\Temp\rtx_bridge_vsr_hdrfix.log`。`scale=1` 即 `D3D11VideoProcessor`，说明 VSR 生效时确实跳过了 D3D12 shader scale chain。此行 `hdr reason=5` 是因为本次未设置 `FFF_PROBE_HDR_MODE`，没有请求 HDR 输出，不是显示能力问题。

### 9.3 真实 HDR 显示能力复核

早先的内联 `HdrProbe2` 返回 `supported=0 enabled=0`，该结果不可靠。用 `C:\Temp\hdr_display_probe.exe` 直接查询 DisplayConfig + DXGI 后确认：

```
path 0 active=1 target=DM72u
advancedColor supported=1 enabled=1 wideColorEnforced=0 bitsPerColorChannel=10
DXGI output colorSpace=RGB_FULL_G2084_NONE_P2020 max=1015.00
R16G16B16A16_FLOAT: scRGB support flags=0x3 (present=1), SetColorSpace1=0
R10G10B10A2_UNORM: scRGB flags=0x0, PQ flags=0x3
B8G8R8A8_UNORM:    scRGB flags=0x0, sRGB flags=0x3
```

因此当前屏幕确实是 HDR，3FP 的 HDR scRGB 输出链本身可用；`OutputPathNotHdr` 不是显示环境造成的，而是 `WantsScRgbPresentationPath` 对 8-bit SDR 源返回 false，在 swap chain 之前就把 `actualMode_` 降成了 `MapToSdr`。旧结论“面板为 SDR”与“8-bit 源只能 OutputPathNotHdr”均已修正。

修复：

- `MapToHdr` 现在默认等效于 `SetVideoAutoHdr(Auto)`：打开 HDR 输出即进入 TrueHDR 请求，不需要第二次调用；8-bit SDR 源因此能进入 scRGB 链。
- 显式 `SetVideoAutoHdr(Off)` 作为 opt-out 保留：它仍可用 HDR 交换链做普通呈现，但不会运行 NVIDIA 的 SDR→HDR 转换。
- 对比模式增加独立硬门：即使色彩模式是 `MapToHdr`，`SetComparisonMode(true)` 也会压掉 TrueHDR，避免驱动生成像素进入帧对比。
- `SetColorMode(MapToHdr)` 同时检查真实 `OutputSupportsHdr()`，避免在 SDR 输出上声称 HDR。

### 9.4 RTX Video HDR 自动触发（真实 HDR 输出，8-bit SDR 源）

只设置 HDR 色彩模式，**不调用** `SetVideoAutoHdr`。命令（不加 `FFF_PROBE_FORCE_HDR`）：

```
FFF_PROBE_HDR_MODE=1 FFF_PROBE_FEATURE_STATUS=1
seek_skip_probe.exe C:\Temp\seek_skip_h264_720p.mp4 gpu12 exact 4.5 skip0
```

实测结果：

```
FEATURE vsr req=0 ... |
        hdr req=1 nv=1 active=1 rejected=0 reason=0 src=1280x720 out=1852x1042 |
        actualColor=2 bitDepth=16 scale=1
mode=gpu decodeMode=2 action=exact ... uploadPath=10 backend=1
```

日志：`C:\Temp\rtx_bridge_auto_hdr_default.log`。`hdr req=1` 是内核按 `MapToHdr` 自动推导出的请求；没有任何 `SetVideoAutoHdr` 调用，HDR 仍真实 `active=1`。

同时打开 VSR 时：

```
FEATURE vsr req=1 ... active=0 reason=7 src=1280x720 tgt=1852x1042 |
        hdr req=1 ... active=1 reason=0 ...
```

日志：`C:\Temp\rtx_bridge_auto_hdr_vsr.log`。`reason=7` 是 `YieldedToAutoHdr`：TrueHDR pass 携带 VSR PPE，VSR 状态让位，不代表 VSR 未请求。

显式 `SetVideoAutoHdr(Off)` 的 opt-out 验证：

```
FEATURE vsr req=0 ... | hdr req=0 active=0 reason=1 |
        actualColor=0 bitDepth=8 scale=0
```

日志：`C:\Temp\rtx_bridge_auto_hdr_off.log`。HDR 交换链不再建立，8-bit 源回到 SDR，证明自动请求可以被明确关闭。

10-bit SDR HEVC 源在同样不加 `FFF_PROBE_FORCE_HDR` 的条件下也保持 `hdr active=1 reason=0 actualColor=2 bitDepth=16`，日志：`C:\Temp\rtx_bridge_hdr_10bit_noforce.log`。

### 9.5 回归

`python FFF.Native.Tests\run_seek_skip_tests.py --ffmpeg C:\PLAN\FFmpeg\ffmpeg.exe --d3d12` 全部通过：8 passed，含 CPU 与 D3D12 的 H.264/HEVC open-GOP seek、frame seek、窗口内 seek。日志：`C:\Temp\rtx_bridge_seek_auto_hdr_final.log`。

### 9.6 已知限制

- 本机 D3D12 的 `FFF3FP_CopyFrame` / `FFF3FP_ReadVideoPixel` 在**关闭 VSR/HDR 时也**返回 `DeviceFailure`，属于既有读回路径限制，不是本次桥接引入；本次激活判定使用公开 feature 状态 API 与真实 `VideoProcessorBlt` HRESULT，未用截图伪造结果。
- HDR 结论已改为真实 HDR 面板验证；不再依赖 `FFF_PROBE_FORCE_HDR`。其他 SDR 面板机器仍会按 `OutputSupportsHdr()` 回退并写 `OutputPathNotHdr`。

### 9.7 窗口缩放重新请求与状态修正（2026-11-28）

验证媒体仍为 `C:\Temp\seek_skip_h264_720p.mp4`（1280x720、8-bit SDR H.264），播放中依次把 client 设为
`1920x1080 → 960x540 → 2560x1440（实际 client 2560x1421）→ 1280x720 → 1920x1080`。
每个尺寸等首帧呈现后暂停 150ms 再读状态，避免读到帧中过渡值。

最终矩阵（D3D12 `gpu12`，D3D11 `gpu`）：

- VSR-only：两个后端一致。1080p `active=1 reason=0`；540p `active=0 reason=3 NotUpscaling`；2526x1422 `active=1 reason=0`；720p 等源尺寸 `active=0 reason=3`；再回 1080p `active=1 reason=0`。目标尺寸随窗口正确写入 `tgt`。
- HDR-only：两个后端所有尺寸均 `hdr active=1 reason=0`，`out` 依次为 1920x1080、960x540、2526x1422、1280x720、1920x1080；`actualColor=2`、`bitDepth=16`。缩小到源以下时 TrueHDR 仍生效，证明 SDR→HDR 不依赖放大。
- VSR+HDR：两个后端 HDR 全部尺寸 `active=1 reason=0`，输出尺寸正确跟随窗口；VSR 在 1080p/2526x1422/再回 1080p 为 `reason=7 YieldedToAutoHdr`（TrueHDR 持有 pass 并携带 VSR PPE），在 540p/720p 等非放大尺寸为 `reason=3 NotUpscaling`，`src/tgt` 均非零。
- 4K 源（3840x2160，超出 VSR 源高上限）在 1080p 窗口下，两个后端均报 `reason=4 SourceResolutionOutOfRange`，不再按目标尺寸误报 `NotUpscaling`。

本轮修正：

- D3D12 bridge 在 VSR 几何门分别记录 `vsrNotUpscaling` / `vsrResolutionOutOfRange`。此前 `lastStage` 先写几何原因、后又被 `TrueHDR blit` 覆盖，`PublishD3D12NvidiaStatus` 落到默认 `DriverRejected`；现在状态按独立标志归因，不再依赖会被覆盖的字符串。
- D3D11 TrueHDR pass 只在源高 360–1440 且目标确实放大时才携带 VSR PPE；同尺寸/缩小不再无意义地设置扩展。TrueHDR 状态块同时写 VSR 源/目标尺寸，并按“源高越界优先、再判断非放大、最后让位 HDR”的顺序归类。
- D3D11 `ApplySuperResolution` 的源高检查移到 `IsUpscaleTarget` 之前，与 D3D12 的门顺序一致；这样 4K 源缩小到 1080p 也报 `SourceResolutionOutOfRange`。

像素级证据：PrintWindow/CopyFromScreen 对 flip-model/D3D12 仍为黑图，改用 Windows.Graphics.Capture 按窗口句柄抓取，已保存
`C:\Temp\wgc_resize2_initial-1080p.png`、`wgc_resize2_shrink-540p.png`、`wgc_resize2_grow-1440p.png`、
`C:\Temp\wgc_d3d11_initial-1080p.png`、`wgc_d3d11_shrink-540p.png`、`wgc_d3d11_regrow-1080p.png`。
截图中 1080p、540p、放大窗口均显示完整测试图，窗口内容随尺寸正确重绘，没有黑帧或残留旧尺寸内容。

回归：`python FFF.Native.Tests\run_seek_skip_tests.py --ffmpeg C:\PLAN\FFmpeg\ffmpeg.exe --d3d12` 仍为 8 passed。
日志：`C:\Temp\final_d3d12_{vsr,hdr,both}.log`、`C:\Temp\final_d3d11_{vsr,hdr,both}.log`、`C:\Temp\final_seek_after_status_fix.log`。
