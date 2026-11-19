// GainMapAppleMeta.h
// ---------------------------------------------------------------------------
// Apple HDR GainMap — exact headroom metadata math (pure header, C++17).
//
// 职责边界（重要）：
//   * 本头只做"已经解析好的数值"之间的数学换算与有效性判定。
//   * 不读文件、不解容器、不依赖 ffmpeg / libavif / 任何外部库。
//   * 字节级 Exif/MakerNote/ISO-BMFF 切分由调用方（GainMapProbe）完成后，
//     把 tag33/tag48 的浮点值与可选的 XMP HDRGainMapVersion 传进来。
//
// Apple 官方语义（developer.apple.com "Applying Apple HDR effect to your
// photos"；johncf/apple-hdr-heic metadata.py 逐字公式）：
//   * 增益图 = 8-bit、单通道（luma）、Rec.709 OETF 编码、约为底图 1/4 像素数
//     （每维约 1/2）。
//   * 施加：hdr_linear = sdr_linear * (1 + (headroom - 1) * gainmap_linear)
//     其中 gainmap_linear = Rec709Inverse( gainByte/255 )。
//   * headroom 来自 Exif MakerNote tag 0x0021(HDRHeadroom=m33) /
//     0x0030(HDRGain=m48) 的 piecewise-stops 公式；HDRGainMapVersion 先判有效性。
//
// 替换目标：GainMapProbe.cpp 里旧的 BuildAppleMeta() 近似映射应改为调用本头。
// ---------------------------------------------------------------------------
#ifndef FFF_NATIVE_3FP_CORE_GAINMAP_APPLE_META_H_
#define FFF_NATIVE_3FP_CORE_GAINMAP_APPLE_META_H_

#include <cmath>
#include <limits>
#include <string>

namespace gainmap_apple {

// ---- 已知 HDRGainMapVersion（XMP: HDRGainMap:HDRGainMapVersion）------------
// Apple 把版本号写成定点：高 16 位 = major，低 16 位 = minor。
constexpr unsigned kHdrGainMapVersion_1_0 = 0x00010000u;  // 65536
constexpr unsigned kHdrGainMapVersion_2_0 = 0x00020000u;  // 131072

// headroom 合理性上界（线性倍率）。超过即视为元数据损坏，fail-closed。
// 8 stops = 256× 已是极宽 HDR，再大必然是误读。
constexpr float kMaxHeadroomLinear = 256.0f;

// piecewise-stops 公式里 m48 的拐点。
constexpr float kGainKnee = 0.01f;

// 解析结果。ok=true 时 headroom*/stops/m33/m48 才有意义。
struct AppleHeadroomResult {
    bool ok = false;
    std::string why;          // 失败原因（ok=false）或来源说明（ok=true 可空）
    float headroom = 1.0f;    // 线性倍率（如 7.37 = 7.37×）
    float headroomLog2 = 0.f; // == max(stops,0)，log2(headroom)
    float stops = 0.f;        // piecewise 原始 stops（clamp 前）
    float m33 = 0.f;          // 回显 tag 0x0021
    float m48 = 0.f;          // 回显 tag 0x0030
};

namespace detail {
inline bool Finite(float v) {
    return std::isfinite(v);
}
inline bool KnownVersion(unsigned v) {
    return v == kHdrGainMapVersion_1_0 || v == kHdrGainMapVersion_2_0;
}
}  // namespace detail

// ---------------------------------------------------------------------------
// 核心：由 MakerNote tag33 / tag48 计算 headroom。
//
//   m33        Exif MakerNote tag 0x0021 (HDRHeadroom)，已解析为浮点。
//   m48        Exif MakerNote tag 0x0030 (HDRGain)，已解析为浮点。
//   versionXmp 可选：XMP HDRGainMapVersion 原始 u32；0 = 本件未携带 XMP 版本
//              （HEIC 的 Apple 件常不写 XMP item，MakerNote 是硬条件，不阻塞）。
//
// 返回 ok=false 时 *绝不* 回退到某个固定倍率（这是私有件踩过的坑：
// tag33/48=0 曾被算成 stops=1.8 ⇒ headroom=3.48× 并错误覆盖 XMP）。
// ---------------------------------------------------------------------------
inline AppleHeadroomResult ComputeHeadroomFromMakerNote(float m33, float m48,
                                                        unsigned versionXmp = 0u) {
    AppleHeadroomResult r;
    r.m33 = m33;
    r.m48 = m48;

    // 0) 版本有效性（仅当 XMP 侧给出版本时校验；缺失不阻塞 HEIC MakerNote 路径）。
    if (versionXmp != 0u && !detail::KnownVersion(versionXmp)) {
        r.why = "HDRGainMapVersion present but unknown/unsupported (0x" +
                std::to_string(versionXmp) + ")";
        return r;
    }

    // 1) 数值健全性：拒绝 NaN / Inf / 负数。
    if (!detail::Finite(m33) || !detail::Finite(m48)) {
        r.why = "tag33/tag48 not finite (NaN/Inf)";
        return r;
    }
    if (m33 < 0.f || m48 < 0.f) {
        r.why = "tag33/tag48 negative (non-monotonic gain map)";
        return r;
    }

    // 2) 0/0 规则：写入器没给 HDR 余量 ⇒ 无有效元数据，拒绝任何固定倍率兜底。
    if (m33 <= 0.f && m48 <= 0.f) {
        r.why = "tag33/tag48 both zero: no HDR headroom declared; refusing fixed-multiplier fallback";
        return r;
    }

    // 3) piecewise-stops 公式（Apple 官方 E5 / johncf metadata.py 逐字）。
    float stops;
    if (m33 < 1.0f) {
        stops = (m48 <= kGainKnee)
                    ? (-20.0f * m48 + 1.8f)
                    : (-0.101f * m48 + 1.601f);
    } else {
        stops = (m48 <= kGainKnee)
                    ? (-70.0f * m48 + 3.0f)
                    : (-0.303f * m48 + 2.303f);
    }
    r.stops = stops;

    // 4) stops<=0 ⇒ 该件声明的余量 ≤ SDR，无 HDR 提升 ⇒ 元数据不可用（不是 SDR）。
    if (!detail::Finite(stops) || stops <= 0.f) {
        r.why = "computed stops <= 0 (no HDR headroom; headroom would be 1.0x)";
        return r;
    }

    float headroom = std::pow(2.0f, stops);  // stops>0 故 headroom>1
    if (!detail::Finite(headroom) || headroom <= 1.0f ||
        headroom > kMaxHeadroomLinear) {
        r.why = "headroom out of sane range (" + std::to_string(headroom) + "x)";
        return r;
    }

    r.ok = true;
    r.headroom = headroom;
    r.headroomLog2 = stops;  // stops>0 时 == log2(headroom)
    r.why = "maker-note piecewise stops";
    return r;
}

// ---------------------------------------------------------------------------
// Rec.709 OETF 逆函数：编码值 [0,1] → 场景线性。
//   E' = 4.5*E            (E < 0.018)
//   E' = 1.099*E^0.45-0.099 (E >= 0.018)
// 逆：
//   E = E'/4.5                 (E' < 0.081)
//   E = ((E'+0.099)/1.099)^(1/0.45)  (E' >= 0.081)
// 输入钳到 [0,1]。
// ---------------------------------------------------------------------------
inline float Rec709Inverse(float v) {
    if (v <= 0.f) return 0.f;
    if (v >= 1.f) return 1.f;
    if (v < 0.081f) {
        return v / 4.5f;
    }
    return std::pow((v + 0.099f) / 1.099f, 1.0f / 0.45f);
}

// ---------------------------------------------------------------------------
// 施加单通道增益到一个线性底图分量。
//   sdrLinear   已经是场景线性的 SDR 底图分量（调用方先做 sRGB/Rec.709 逆变换）。
//   gainByte01  增益图像素，已归一化到 [0,1]（byte/255）。
//   headroomLinear  ComputeHeadroomFromMakerNote().headroom。
// 返回：hdr_linear = sdrLinear * (1 + (headroom-1)*Rec709Inverse(gainByte01))。
// 单通道增益同时乘 R/G/B（Apple 语义：三通道同一标量增益，不做逐通道分离）。
// ---------------------------------------------------------------------------
inline float ApplyGainComponent(float sdrLinear, float gainByte01,
                                float headroomLinear) {
    float g = Rec709Inverse(gainByte01);
    float scale = 1.0f + (headroomLinear - 1.0f) * g;
    return sdrLinear * scale;
}

// 理论最大提升倍率：增益图 255 → Rec709Inv(1.0)=1.0 → scale = headroom。
inline float MaxBoost(float headroomLinear) { return headroomLinear; }
}  // namespace gainmap_apple

#endif  // FFF_NATIVE_3FP_CORE_GAINMAP_APPLE_META_H_
