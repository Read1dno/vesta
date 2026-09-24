#pragma once
#include <core/memory/compatibility.hpp>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <span>
#include <type_traits>

namespace game::compatibility::detail {
template<class T> T field(std::span<const std::byte> bytes, std::size_t at)
{
    T value{};
    if (at <= bytes.size() && sizeof(T) <= bytes.size()-at)
        std::memcpy(&value, bytes.data()+at, sizeof(T));
    return value;
}
template<typename T>
bool convar_value(std::span<const std::byte> bytes, T& value)
{
    static_assert(std::is_same_v<T,bool> || std::is_same_v<T,int> || std::is_same_v<T,float>);
    value={};
    if (bytes.size()<0x5c) return false;
    constexpr auto expected=std::is_same_v<T,bool> ? 0u : std::is_same_v<T,int> ? 3u : 7u;
    if (field<std::uint32_t>(bytes,0x28)!=expected) return false;
    if constexpr(std::is_same_v<T,bool>) {
        const auto raw=std::to_integer<unsigned>(bytes[0x58]);
        if(raw>1) return false;
        value=raw!=0;
    } else {
        value=field<T>(bytes,0x58);
        if constexpr(std::is_same_v<T,float>) if(!std::isfinite(value)) {value={};return false;}
    }
    return true;
}
inline std::uint32_t decode_shot_punch(std::span<const std::byte> code)
{
    if(code.size()<32 || field<std::uint32_t>(code,18)!=0x8e110ff2u
        || field<std::uint16_t>(code,26)!=0x8689u) return 0;
    const auto xy=field<std::uint32_t>(code,22), z=field<std::uint32_t>(code,28);
    return xy>=0x100 && xy<=0x4000 && z==xy+8 && xy%4==0 ? xy : 0;
}
inline std::optional<radar_layout> decode_radar(std::span<const std::byte> code)
{
    if (code.size() < 60) return {};
    const auto x = field<std::uint32_t>(code, 56);
    const auto y = field<std::uint32_t>(code, 47);
    const auto canvas = field<std::uint32_t>(code, 26);
    const auto viewport = field<std::uint32_t>(code, 18);
    const auto alternate = field<std::uint32_t>(code, 8);
    if (x < 0x100 || x > 0x1000 || y != x+4 || canvas != x+12
        || viewport != x+16 || alternate < 0x10000 || alternate > 0x40000) return {};
    return radar_layout{x-0x20, alternate-0x20};
}
inline std::uint32_t float_getter(std::span<const std::byte> code)
{
    if (code.size() < 9 || field<std::uint32_t>(code,0) != 0x81100ff3
        || code[8] != std::byte{0xc3}) return 0;
    const auto offset = field<std::uint32_t>(code,4);
    return offset >= 0x100 && offset <= 0x800 ? offset : 0;
}
inline bool valid_frame(const crosshair_frame& frame)
{
    if (frame.count > frame.pieces.size()) return false;
    for (std::size_t i=0; i<frame.count; ++i) {
        const auto& p=frame.pieces[i];
        if (p.type > 2) return false;
        for (float v:p.geometry) if (!std::isfinite(v) || std::abs(v)>32768) return false;
        for (const auto& color:{p.color,p.outline})
            for (float v:color) if (!std::isfinite(v) || v<0 || v>1.01f) return false;
        if (!std::isfinite(p.angle) || !std::isfinite(p.rotation)
            || std::abs(p.angle)>6.284f || std::abs(p.rotation)>6.284f) return false;
        if (p.type==0 && (p.geometry[2]<p.geometry[0] || p.geometry[3]<p.geometry[1])) return false;
        if (p.type!=0 && (p.geometry[2]<0 || p.geometry[3]<0 || p.geometry[3]>p.geometry[2]*2)) return false;
    }
    return true;
}
template<class Reader>
bool coherent_frame(std::uintptr_t sequence, std::uintptr_t buffers, Reader&& read,
    crosshair_frame& output, std::uint32_t& published)
{
    output={};published=0;
    if(!sequence || !buffers) return false;
    for(int attempt=0;attempt<3;++attempt) {
        std::uint32_t before{},after{};
        crosshair_frame sample{};
        if(!read(sequence,&before,sizeof(before)) || !before
            || !read(buffers+(before&1)*sizeof(sample),&sample,sizeof(sample))
            || !read(sequence,&after,sizeof(after))) return false;
        if(before!=after) continue;
        if(!valid_frame(sample)) return false;
        output=sample;published=before;return true;
    }
    return false;
}
inline float linear_to_srgb(float v)
{
    v=std::clamp(v,0.0f,1.0f);
    return v<=0.0031308f ? v*12.92f : 1.055f*std::pow(v,1.0f/2.4f)-0.055f;
}
} // namespace game::compatibility::detail
