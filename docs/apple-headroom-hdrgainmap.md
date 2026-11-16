# Apple HDR GainMap — Headroom 元数据精确落地

> 配套纯头实现：`FFF.Native/3FP/Core/GainMapAppleMeta.h`
> 配套可执行测试：`FFF.Native/tools/apple_meta_probe.cpp`（`build_apple_meta.bat` 编译）
>
> 范围：只做"已解析数值 → headroom / 施加系数"的数学与有效性判定。
> 字节级 Exif/MakerNote/ISO-BMFF 切分由 `GainMapProbe` 负责，本模块不读文件、不依赖 ffmpeg。

## 1. Apple 官方语义（事实基线）

来源：Apple Developer《Applying Apple HDR effect to your photos》
（<https://developer.apple.com/documentation/appkit/applying-apple-hdr-effect-to-your-photos>），
以及一手参考实现 johncf/apple-hdr-heic `metadata.py` / `lib.py`。

- **增益图资产**：`urn:com:apple:photo:2020:aux:hdrgainmap` 辅助图。
  - **8-bit**、**单通道**（luma）、**Rec.709 OETF 编码**、约为底图 **1/4 像素数**（每维约 1/2）。
- **施加公式**（线性域）：

  ```
  gainmap_linear = Rec709Inverse( gainByte / 255 )
  hdr_linear     = sdr_linear * ( 1 + (headroom - 1) * gainmap_linear )
  ```

  - 增益字节先归一化到 `[0,1]`，**再做 Rec.709 逆变换到线性**，然后乘进 `(headroom-1)`。
  - 单通道亮度增益**同时**乘 R/G/B 三个通道（同一标量，不做逐通道分离）。
  - 这是线性内插，**不是** `2^logBoost`。
- **headroom 来源**：Exif MakerNote `tag 0x0021 (HDRHeadroom=m33)` 与 `tag 0x0030 (HDRGain=m48)`，
  经 **piecewise stops** 公式换算；先由 `HDRGainMapVersion`（XMP 侧）判有效性。

### Rec.709 逆传递函数

ITU-R BT.709 OETF：`E' = 4.5·E (E<0.018)`；`E' = 1.099·E^0.45 - 0.099 (E≥0.018)`。逆：

```
E = E'/4.5                          (E' < 0.081)
E = ((E' + 0.099)/1.099)^(1/0.45)   (E' ≥ 0.081)
```

端点：`gainByte=0 → linear=0 → scale=1`；`gainByte=255 → linear=1 → scale=headroom`。

## 2. piecewise stops 公式（Apple 官方 E5 / johncf metadata.py 逐字）

```
if m33 < 1.0:
    stops = -20.0·m48 + 1.8      当 m48 ≤ 0.01
    stops = -0.101·m48 + 1.601   当 m48 >  0.01
else:  # m33 ≥ 1.0
    stops = -70.0·m48 + 3.0      当 m48 ≤ 0.01
    stops = -0.303·m48 + 2.303   当 m48 >  0.01
headroom    = 2^max(stops, 0)
headroomLog2 = max(stops, 0)
```

代码即 `gainmap_apple::ComputeHeadroomFromMakerNote()`。

## 3. 真实件实测数字（验收基准）

件：`testmedia/gainmap-corpus/external/apple_hdr_sample_imazen.heic`（238 982 B，
imazen/heic `testdata/apple-hdr`，真 iPhone HDR HEIC）。由 `apple_meta_probe.exe` 现场解出并断言。

| 量 | 值 | 说明 |
|---|---|---|
| 主图 item | id=7（grid） | pitm=7 |
| 底图尺寸 | **1512 × 850** | ispe（item7，pixi 3×8bit） |
| 增益图 item | id=10（hvc1） | auxC=`urn:com:apple:photo:2020:aux:hdrgainmap` |
| 增益图尺寸 | **756 × 426** | ispe（item10） |
| 增益图位深/通道 | **单通道 8-bit** | pixi ch=1 bits=[8] |
| 分辨率比例 W | 1512/756 = **2.000** | 每维 1/2 |
| 分辨率比例 H | 850/426 = **1.995** | |
| 像素面积比 | 756·426 / 1512·850 = **0.2506 ≈ 1/4** | 符合"1/4 分辨率" |
| XMP HDRGainMapVersion | **65536 = 0x10000 = v1.0** | 无 HDRGainMapHeadroom 字段 |
| MakerNote tag33 (0x0021, HDRHeadroom) | **117926/114993 = 1.025506** | type=10 SRATIONAL |
| MakerNote tag48 (0x0030, HDRGain) | **769/456192 = 0.001686** | type=10 SRATIONAL |
| 命中分支 | m33≥1.0，m48≤0.01 | |
| stops 中间量 | -70·0.001686 + 3.0 = **2.882001** | |
| **headroom** | 2^2.882001 = **7.371721 ×** | log2 = 2.882 |
| **理论最大 boost** | gainByte=255 → **7.37 ×** | scale=headroom |
| 理论最小 boost | gainByte=0 → **1.00 ×** | scale=1 |

同一组数字固化为 `gainmap_apple::AppleImazenVector`，`RunSelfTest()` 与探针都断言它。
探针全绿输出见 `build_out/` 运行结果（OVERALL: PASS）。

## 4. 有效性 / 判无效规则（fail-closed，不回退固定倍率）

`ComputeHeadroomFromMakerNote(m33, m48, versionXmp)` 返回 `{ok, why, headroom, ...}`。
以下任一命中即 `ok=false`，**绝不**猜测一个默认倍率：

| 情况 | 判定 | why |
|---|---|---|
| `HDRGainMapVersion` 缺失（=0） | **通过**（HEIC Apple 件常不写 XMP item，MakerNote 是硬条件） | — |
| `HDRGainMapVersion` 存在但非 0x10000/0x20000 | **无效** | `...unknown/unsupported` |
| tag33/tag48 为 NaN/Inf | 无效 | `not finite` |
| tag33<0 或 tag48<0（非单调） | 无效 | `negative (non-monotonic)` |
| **tag33=0 且 tag48=0** | **无效**（私有写入器没给余量；旧坑：曾被算成 stops=1.8⇒3.48× 并错盖 XMP） | `both zero ... refusing fixed-multiplier fallback` |
| 计算后 stops ≤ 0 | 无效（headroom 会=1.0，无 HDR） | `stops <= 0` |
| headroom≤1 或 >256×（8 stops 上界） | 无效（元数据损坏） | `out of sane range` |

> 关键红线：`tag33/48=0/0` 必须判无效，**不得**退化成 3.48×/8× 等固定倍率。
> 手机私有件（DJI/MiHE 等）常写 0/0，正确做法是交回 XMP 判定，而不是自己编一个 headroom。

### 8-bit 增益归一化与单通道语义

- 归一化：`gainByte/255 → [0,1]`，**随后**过 Rec.709 逆变换到线性；不要拿字节直接当线性权重。
- 单通道增益：对 R/G/B 三通道施加**同一个** `scale`（`out[c]=sdr[c]*scale`），不逐通道分离。

## 5. 与参考实现的差异（以 Apple 官方文档为准）

参考实现：`ffmpegPictureUI` 的 `GainMapDecoder.cs` / `AppleMakerNoteParser.cs`。

1. **piecewise stops 公式本身与参考逐字一致**（参考注释亦引自 Apple E5 / johncf metadata.py），
   本头未改系数。
2. **MakerNote tag 编码覆盖更全**：参考 `AppleMakerNoteParser` 只认
   type=5（RATIONAL 无符号对）与 type=11（FLOAT32 inline）。**本真实件 tag33/48 是 type=10**
   （8 字节带符号 SRATIONAL：两个 s32，值在 MN 起 + offset 处）。参考纯托管臂会在此失败并
   **降级 exiftool**；本头/探针同时认 type=5/9/10（有理对）与 type=11（float），不依赖外部进程。
   - 数值：m33=117926/114993=1.0255，m48=769/456192=0.001686（探针实测）。
3. **v1.0 无 XMP headroom 字段时的差异**：参考 XMP 路径对 v1.0（无 `HDRGainMapHeadroom`）
   采用**隐式 8×**（log2=3.0）。本件走 MakerNote 精确解得 **7.37×**。
   - 隐式 8× 比精确值高约 **8.5%**（log2 差 0.118 stops）；这是参考实现"近似"的来源。
   - 当 MakerNote 可读时应以 MakerNote 的 7.37× 为准；MakerNote 缺失才退到 XMP/隐式值。
4. **旧 3FP `BuildAppleMeta`（GainMapProbe.cpp）**：piecewise 系数与本头相同，但缺
   版本门、缺 headroom 上界护栏、且解析侧未覆盖 type=10 SRATIONAL。本头即其精确替换；
   主实现者 `#include "GainMapAppleMeta.h"` 后调用 `ComputeHeadroomFromMakerNote()` 即可。

## 6. 接入方式

```cpp
#include "3FP/Core/GainMapAppleMeta.h"
using namespace gainmap_apple;

// 调用方已从 MakerNote 解出 tag33/tag48 浮点、从 XMP 解出版本（0=缺失）：
AppleHeadroomResult r = ComputeHeadroomFromMakerNote(m33, m48, xmpVersion);
if (!r.ok) { /* 按 r.why 记日志，fail-closed，不兜底 */ }
float headroom = r.headroom;            // 7.37×
// 施加（每个像素、每个线性分量）：
float hdr = ApplyGainComponent(sdrLinear, gainByte / 255.f, headroom);
```

自测：`gainmap_apple::RunSelfTest(report)` 编译期无关、可在宿主启动时跑一遍；
独立验证见 `tools/build_out/apple_meta_probe.exe`。
