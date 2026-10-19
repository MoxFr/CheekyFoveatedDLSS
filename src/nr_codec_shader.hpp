#pragma once
#include <cstdint>
namespace cheeky::foveated_dlss {
struct CodecConstants {
        std::uint32_t size[2];
        std::uint32_t source_size[2];
        std::uint32_t source_base[2];
        std::uint32_t proxy_size[2];
        float paper_white_scale;
        float transfer_strength;
        float color_strength;
        std::uint32_t hdr_mode;
        std::uint32_t region_base[2];
        std::uint32_t region_size[2];
        float foveation_width;
        float foveation_height;
        float foveation_roundness;
        float foveation_feather;
        std::uint32_t show_alignment_border;
        std::uint32_t mask_count, reconstruction, filter_flags;
        float mask_bounds[4][4];
        std::uint32_t color_pipeline, signal_encoding, exposure_mode, exposure_available;
        float exposure_multiplier;
        std::uint32_t scaling_domain, color_padding[2];
    };
    static_assert(sizeof(CodecConstants) == 48U * sizeof(std::uint32_t));
inline constexpr char nr_codec_shader[] = R"(
Texture2D<float4> Source0 : register(t0);
Texture2D<float4> Source1 : register(t1);
Texture2D<float4> Source2 : register(t2);
#ifndef CHEEKY_NR_NO_EXPOSURE
Texture2D<float4> Exposure : register(t3);
#endif
RWTexture2D<float4> Output0 : register(u0);
RWTexture2D<float4> Output1 : register(u1);

cbuffer CodecConstants : register(b0) {
    uint2 Size;
    uint2 SourceSize;
    uint2 SourceBase;
    uint2 ProxySize;
    float PaperWhiteScale;
    float TransferStrength;
    float ColorStrength;
    uint HdrMode;
    uint2 RegionBase;
    uint2 RegionSize;
    float FoveationWidth;
    float FoveationHeight;
    float FoveationRoundness;
    float FoveationFeather;
    uint ShowAlignmentBorder;
    uint MaskCount;
    uint Reconstruction;
    uint FilterFlags; // bit 0: area downsample; bit 1: bicubic upsample
    float4 MaskBounds[4];
    uint ColorPipeline;
    uint SignalEncoding;
    uint ExposureMode;
    uint ExposureAvailable;
    float ExposureMultiplier;
    uint ScalingDomain;
    uint2 ColorPadding;
};

float FoveationShapeDistance(float2 pixel) {
    if (MaskCount != 0) {
        const float2 uv = (pixel - float2(RegionBase) + 0.5) / max(float2(RegionSize), 1.0);
        float distance = 1e10;
        [unroll] for (uint i = 0; i < 4; ++i) if (i < MaskCount) {
            const float4 b = MaskBounds[i];
            const float2 p = abs(2.0 * uv - b.xy - b.zw) / max(b.zw - b.xy, 0.0001);
            distance = min(distance, lerp(max(p.x, p.y), length(p), saturate(FoveationRoundness)));
        }
        return distance;
    }
    const float2 centered =
        (pixel - float2(RegionBase) + 0.5) /
        (0.5 * max(float2(RegionSize), 1.0)) - 1.0;
    const float2 scaled = abs(centered);
    return lerp(
        max(scaled.x, scaled.y),
        length(scaled),
        saturate(FoveationRoundness)
    );
}

float Luminance(float3 color) {
    return dot(color, float3(0.2126, 0.7152, 0.0722));
}

float SrgbEncodeChannel(float value) {
    value = saturate(value);
    return value <= 0.0031308
        ? 12.92 * value
        : 1.055 * pow(value, 1.0 / 2.4) - 0.055;
}

float3 SrgbEncode(float3 color) {
    return float3(
        SrgbEncodeChannel(color.r),
        SrgbEncodeChannel(color.g),
        SrgbEncodeChannel(color.b)
    );
}

float SrgbDecodeChannel(float value) {
    value = saturate(value);
    return value <= 0.04045
        ? value / 12.92
        : pow((value + 0.055) / 1.055, 2.4);
}

float3 SrgbDecode(float3 color) {
    return float3(
        SrgbDecodeChannel(color.r),
        SrgbDecodeChannel(color.g),
        SrgbDecodeChannel(color.b)
    );
}

// Reconstructed from RenoDX's shipped TransformConstants shaders (2026-10-17).
// Native linear BT.709 and simple sRGB are verified; other gamut paths are not.
float SignedSrgb(float x, bool encode) {
    float a = abs(x);
    float v = encode ? (a <= 0.0031308 ? 12.92*a : 1.055*pow(a,1.0/2.4)-0.055)
                     : (a <= 0.04045 ? a/12.92 : pow((a+0.055)/1.055,2.4));
    return x < 0.0 ? -v : v;
}
float SignedPq(float x, bool encode) {
    float a=abs(x), v;
    if (encode) {
        float p=pow(a,2610.0/16384.0);
        v=pow((3424.0/4096.0+(2413.0/128.0)*p)/(1.0+(2392.0/128.0)*p),2523.0/32.0);
    } else {
        float p=pow(a,32.0/2523.0);
        v=pow(max(p-3424.0/4096.0,0.0)/max(2413.0/128.0-(2392.0/128.0)*p,0.000001),16384.0/2610.0);
    }
    return x < 0.0 ? -v : v;
}
float3 SignalConvert(float3 rgb, uint encoding, bool encode) {
    if (encoding == 2U) return rgb;
    if (encoding == 1U) return float3(SignedPq(rgb.r,encode),SignedPq(rgb.g,encode),SignedPq(rgb.b,encode));
    return float3(SignedSrgb(rgb.r,encode),SignedSrgb(rgb.g,encode),SignedSrgb(rgb.b,encode));
}
float EffectiveExposure() {
    float value=1.0;
#ifndef CHEEKY_NR_NO_EXPOSURE
    if (ExposureAvailable != 0U && ExposureMode != 0U) value=Exposure.Load(int3(0,0,0)).r;
#endif
    if (!isfinite(value) || value <= 0.0) value=1.0;
    if (ExposureMode == 2U) value=1.0/value;
    // Finite safety bounds intentionally exceed ordinary exposure ranges.
    return clamp(value*ExposureMultiplier,0.000001,1000000.0);
}
float3 BridgeNormalized(float3 rgb) {
    if (ColorPipeline == 2U) rgb=SignalConvert(rgb,0U,false);
    return rgb * EffectiveExposure()/max(PaperWhiteScale,0.0001);
}
float BridgeScale(float3 normalized) {
    float m=max(abs(normalized.r),max(abs(normalized.g),abs(normalized.b)));
    return rsqrt(1.0+m*m);
}
float3 BridgeEncode(float3 rgb) {
    float3 normalized=BridgeNormalized(rgb);
    return ColorPipeline == 1U ? saturate(normalized*BridgeScale(normalized)) : normalized;
}
float3 BridgeRestore(float3 edited, float3 original) {
    if (ColorPipeline == 1U) edited/=max(BridgeScale(BridgeNormalized(original)),0.000001);
    edited*=max(PaperWhiteScale,0.0001)/EffectiveExposure();
    return ColorPipeline == 2U ? SignalConvert(edited,0U,true) : edited;
}
float3 ScaleEncode(float3 rgb) {
    if (ScalingDomain == 1U) rgb*=PaperWhiteScale*0.01; // paper-white nits / 10000
    return SignalConvert(rgb,ScalingDomain,true);
}
float3 ScaleDecode(float3 rgb) {
    rgb=SignalConvert(rgb,ScalingDomain,false);
    return ScalingDomain == 1U ? rgb/max(PaperWhiteScale*0.01,0.000001) : rgb;
}
float4 LoadFilterSource(int2 p) {
    float4 value=Source0.Load(int3(p,0));
    if (ColorPipeline != 0U) value.rgb=ScaleEncode(BridgeEncode(value.rgb));
    return value;
}
float4 LoadFilterProxy(int2 p) {
    float4 value=Source1.Load(int3(p,0));
    if (ColorPipeline != 0U) value.rgb=ScaleEncode(SignalConvert(value.rgb,SignalEncoding,false));
    return value;
}
float4 LoadFilterNeural(int2 p) {
    float4 value=Source2.Load(int3(p,0));
    if (ColorPipeline != 0U) value.rgb=ScaleEncode(SignalConvert(value.rgb,SignalEncoding,false));
    return value;
}

float4 LoadSource0Bilinear(float2 position, uint2 origin, uint2 dimensions) {
    const float2 base = floor(position);
    const float2 fraction = position - base;
    const int2 minimum = int2(origin);
    const int2 maximum = minimum + int2(dimensions) - 1;
    const int2 p00 = clamp(int2(base), minimum, maximum);
    const int2 p10 = clamp(int2(base) + int2(1, 0), minimum, maximum);
    const int2 p01 = clamp(int2(base) + int2(0, 1), minimum, maximum);
    const int2 p11 = clamp(int2(base) + int2(1, 1), minimum, maximum);
    return lerp(
        lerp(LoadFilterSource(p00), LoadFilterSource(p10), fraction.x),
        lerp(LoadFilterSource(p01), LoadFilterSource(p11), fraction.x),
        fraction.y
    );
}

float4 LoadSource1Bilinear(float2 position, uint2 dimensions) {
    const float2 base = floor(position);
    const float2 fraction = position - base;
    const int2 maximum = int2(dimensions) - 1;
    const int2 p00 = clamp(int2(base), int2(0, 0), maximum);
    const int2 p10 = clamp(int2(base) + int2(1, 0), int2(0, 0), maximum);
    const int2 p01 = clamp(int2(base) + int2(0, 1), int2(0, 0), maximum);
    const int2 p11 = clamp(int2(base) + int2(1, 1), int2(0, 0), maximum);
    return lerp(
        lerp(LoadFilterProxy(p00), LoadFilterProxy(p10), fraction.x),
        lerp(LoadFilterProxy(p01), LoadFilterProxy(p11), fraction.x),
        fraction.y
    );
}

float4 LoadSource2Bilinear(float2 position, uint2 dimensions) {
    const float2 base = floor(position);
    const float2 fraction = position - base;
    const int2 maximum = int2(dimensions) - 1;
    const int2 p00 = clamp(int2(base), int2(0, 0), maximum);
    const int2 p10 = clamp(int2(base) + int2(1, 0), int2(0, 0), maximum);
    const int2 p01 = clamp(int2(base) + int2(0, 1), int2(0, 0), maximum);
    const int2 p11 = clamp(int2(base) + int2(1, 1), int2(0, 0), maximum);
    return lerp(
        lerp(LoadFilterNeural(p00), LoadFilterNeural(p10), fraction.x),
        lerp(LoadFilterNeural(p01), LoadFilterNeural(p11), fraction.x),
        fraction.y
    );
}

// Integrate the complete source-pixel footprint, including fractional edge pixels.
// The loop covers the actual rounded working extent, including both edge texels.
float4 LoadSource0Area(uint2 pixel) {
    const float2 ratio = float2(SourceSize) / float2(ProxySize);
    const float2 lo = float2(SourceBase) + float2(pixel) * ratio;
    const float2 hi = min(float2(SourceBase + SourceSize), lo + ratio);
    const int2 first = int2(floor(lo));
    const int2 end = int2(ceil(hi));
    const int2 maximum = int2(SourceBase + SourceSize) - 1;
    float4 sum = 0.0;
    float total = 0.0;
    [loop] for (int y = first.y; y < end.y; ++y) {
        const float wy = max(0.0, min(hi.y, float(y + 1)) - max(lo.y, float(y)));
        [loop] for (int x = first.x; x < end.x; ++x) {
            const float weight = wy * max(0.0, min(hi.x, float(x + 1)) - max(lo.x, float(x)));
            sum += weight * LoadFilterSource(clamp(int2(x,y), int2(SourceBase), maximum));
            total += weight;
        }
    }
    return sum / max(total, 0.000001);
}

float4 CubicWeights(float t) {
    const float t2 = t*t;
    const float t3 = t2*t;
    return float4(-0.5*t + t2 - 0.5*t3, 1.0 - 2.5*t2 + 1.5*t3,
                  0.5*t + 2.0*t2 - 1.5*t3, -0.5*t2 + 0.5*t3);
}

void LoadProxyNeuralBicubic(float2 position, out float4 proxy, out float4 neural) {
    const int2 base = int2(floor(position));
    const float2 fraction = position - floor(position);
    const float4 wx = CubicWeights(fraction.x);
    const float4 wy = CubicWeights(fraction.y);
    const int2 maximum = int2(ProxySize) - 1;
    proxy = 0.0; neural = 0.0;
    float4 pmin = 1e30, pmax = -1e30, nmin = 1e30, nmax = -1e30;
    [unroll] for (int y = 0; y < 4; ++y) {
        [unroll] for (int x = 0; x < 4; ++x) {
            const int3 p = int3(clamp(base + int2(x-1,y-1), int2(0,0), maximum), 0);
            const float4 a = LoadFilterProxy(p.xy);
            const float4 b = LoadFilterNeural(p.xy);
            proxy += a * wx[x] * wy[y];
            neural += b * wx[x] * wy[y];
            if (x >= 1 && x <= 2 && y >= 1 && y <= 2) {
                pmin = min(pmin,a); pmax = max(pmax,a);
                nmin = min(nmin,b); nmax = max(nmax,b);
            }
        }
    }
    // Limit negative lobes to the central footprint to avoid bright/dark halos.
    proxy = clamp(proxy,pmin,pmax);
    neural = clamp(neural,nmin,nmax);
}

float3 UpgradeToneMap(float3 original, float3 proxy, float3 neural) {
    float original_y = Luminance(original);
    float proxy_y = Luminance(proxy);
    float neural_y = Luminance(neural);
    float ratio;
    if (original_y < proxy_y) {
        ratio = proxy_y > 0.0 ? original_y / proxy_y : 0.0;
    } else {
        float new_y = neural_y + max(0.0, original_y - proxy_y);
        ratio = neural_y > 0.0 ? new_y / neural_y : 0.0;
    }
    return lerp(original, max(neural * ratio, 0.0), TransferStrength);
}

[numthreads(16, 16, 1)]
void EncodeMain(uint3 dispatch_id : SV_DispatchThreadID) {
    if (all(dispatch_id.xy < Size)) {
        Output0[dispatch_id.xy] = Source0.Load(int3(SourceBase + dispatch_id.xy, 0));
    }
    if (all(dispatch_id.xy < ProxySize)) {
        const float2 source_position =
            float2(SourceBase) +
            (float2(dispatch_id.xy) + 0.5) * float2(SourceSize) /
            float2(ProxySize) - 0.5;
        const float4 proxy_source = (FilterFlags & 1U) != 0U ? LoadSource0Area(dispatch_id.xy) : LoadSource0Bilinear(
            source_position,
            SourceBase,
            SourceSize
        );
        const float3 linear_color = max(
            proxy_source.rgb / max(PaperWhiteScale, 0.0001),
            0.0
        );
        const float3 encoded = ColorPipeline != 0U
            ? SignalConvert(ScaleDecode(proxy_source.rgb),SignalEncoding,true)
            : (HdrMode != 0 ? SrgbEncode(linear_color) : proxy_source.rgb);
        Output1[dispatch_id.xy] = float4(encoded, proxy_source.a);
    }
}

[numthreads(16, 16, 1)]
void BorderMain(uint3 dispatch_id : SV_DispatchThreadID) {
    if (any(dispatch_id.xy >= Size)) return;
    const float distance = FoveationShapeDistance(dispatch_id.xy);
    const float step = max(
        abs(FoveationShapeDistance(float2(dispatch_id.xy) + float2(1, 0)) - distance),
        abs(FoveationShapeDistance(float2(dispatch_id.xy) + float2(0, 1)) - distance));
    if (distance <= 1.0 && distance >= 1.0 - 5.0 * step)
        Output0[SourceBase + dispatch_id.xy] = float4(0, 1, 0, 1);
}

[numthreads(16, 16, 1)]
void DecodeMain(uint3 dispatch_id : SV_DispatchThreadID) {
    if (any(dispatch_id.xy >= Size)) return;
    const float4 original_sample = Source0.Load(int3(dispatch_id.xy, 0));
    const bool inside_region = all(dispatch_id.xy >= RegionBase) &&
        all(dispatch_id.xy < RegionBase + RegionSize);
    const float distance_from_center = FoveationShapeDistance(dispatch_id.xy);
    const float distance_per_pixel = max(
        abs(FoveationShapeDistance(float2(dispatch_id.xy) + float2(1.0, 0.0)) -
            distance_from_center),
        abs(FoveationShapeDistance(float2(dispatch_id.xy) + float2(0.0, 1.0)) -
            distance_from_center)
    );
    const bool alignment_border = ShowAlignmentBorder != 0U &&
        inside_region && distance_from_center <= 1.0 &&
        distance_from_center >= 1.0 - 5.0 * distance_per_pixel;
    if (alignment_border) {
        Output0[SourceBase + dispatch_id.xy] = float4(0.0, 1.0, 0.0, 1.0);
        return;
    }
    const float normalized_feather = FoveationFeather /
        max(0.0001, min(FoveationWidth, FoveationHeight));
    const float foveation_weight = FoveationFeather <= 0.0
        ? (distance_from_center <= 1.0 ? 1.0 : 0.0)
        : 1.0 - smoothstep(
            max(0.0, 1.0 - normalized_feather),
            1.0,
            distance_from_center
        );
    if (!inside_region || foveation_weight <= 0.0) {
        Output0[SourceBase + dispatch_id.xy] = original_sample;
        return;
    }
    const float2 proxy_position =
        (float2(dispatch_id.xy - RegionBase) + 0.5) * float2(ProxySize) /
        float2(RegionSize) - 0.5;
    float4 proxy_sample, neural_sample;
    if ((FilterFlags & 2U) != 0U) {
        LoadProxyNeuralBicubic(proxy_position, proxy_sample, neural_sample);
    } else {
        proxy_sample = LoadSource1Bilinear(proxy_position, ProxySize);
        neural_sample = LoadSource2Bilinear(proxy_position, ProxySize);
    }
    if (ColorPipeline != 0U) {
        // Residual is added in the compressed bridge's scaling domain, BEFORE
        // restoring the original-derived HDR scale (not in raw game RGB).
        const float3 basis=ScaleEncode(BridgeEncode(original_sample.rgb));
        const float3 edited=Reconstruction == 1U
            ? basis+neural_sample.rgb-proxy_sample.rgb : neural_sample.rgb;
        const float3 restored=BridgeRestore(ScaleDecode(edited),original_sample.rgb);
        Output0[SourceBase + dispatch_id.xy]=float4(lerp(original_sample.rgb,restored,
            TransferStrength*ColorStrength*foveation_weight),original_sample.a);
        return;
    }
    if (HdrMode == 0) {
        // Transfer the model's edit, not the lower-resolution source detail.
        const float3 target = Reconstruction == 1U
            ? max(original_sample.rgb + (neural_sample.rgb - proxy_sample.rgb), 0.0)
            : neural_sample.rgb;
        const float4 processed = float4(lerp(original_sample.rgb, target,
            TransferStrength * ColorStrength), original_sample.a);
        Output0[SourceBase + dispatch_id.xy] = lerp(original_sample, processed, foveation_weight);
        return;
    }
    const float3 original = max(
        original_sample.rgb / max(PaperWhiteScale, 0.0001),
        0.0
    );
    const float3 proxy = SrgbDecode(proxy_sample.rgb);
    const float3 neural = SrgbDecode(neural_sample.rgb);
    const float3 upgraded = Reconstruction == 1U
        ? lerp(original, max(original + (neural - proxy), 0.0), TransferStrength)
        : UpgradeToneMap(original, proxy, neural);
    const float3 decoded = lerp(original, upgraded, ColorStrength) * PaperWhiteScale;
    const float4 processed = float4(max(decoded, 0.0), original_sample.a);
    Output0[SourceBase + dispatch_id.xy] = lerp(original_sample, processed, foveation_weight);
}
)";
}
