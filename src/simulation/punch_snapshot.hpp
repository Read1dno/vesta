#pragma once
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <numbers>
#include <core/math/vector.hpp>

namespace simulation {
// Cache-tail compatibility reader; this does not establish the time of a future shot.
template<class Reader>
std::optional<foundation::vec3> read_cached_punch(Reader& reader, std::uintptr_t pawn,
    std::ptrdiff_t services_offset)
{
    struct sample_vector {
        std::int32_t size{}, padding{};
        std::uintptr_t data{};
        std::int32_t capacity{};
        std::uint32_t flags{};
    };
    static_assert(sizeof(sample_vector)==24);
    constexpr std::uintptr_t highest=0x00007fffffffffffULL;
    const auto address=[](std::uintptr_t value,std::size_t size) {
        return value>=0x10000 && size && value<=highest-(size-1);
    };
    const auto valid=[](const foundation::vec3& value) {
        return std::isfinite(value.x)&&std::isfinite(value.y)&&std::isfinite(value.z)
            && std::abs(value.x)<45 && std::abs(value.y)<45 && std::abs(value.z)<45;
    };
    if(services_offset<=0 || services_offset>0x4000
        || !address(pawn,services_offset+sizeof(std::uintptr_t))) return {};
    std::uintptr_t services{};
    if(!reader.copy(pawn+services_offset,&services,sizeof(services))
        || !address(services,0xe8)) return {};

    constexpr std::size_t start=0x48;
    std::array<std::byte,0xa0> before{},after{};
    if(!reader.copy(services+start,before.data(),before.size())) return {};
    std::array<foundation::vec3,2> values{};
    std::array<std::uintptr_t,2> tails{};
    constexpr std::array<std::size_t,2> descriptors{0x88,0xd0},bases{0x50,0xa4};
    for(std::size_t i=0;i<values.size();++i) {
        sample_vector vector{};
        std::memcpy(&vector,before.data()+descriptors[i]-start,sizeof(vector));
        if(vector.size==0) {
            std::memcpy(&values[i],before.data()+bases[i]-start,sizeof(values[i]));
        } else {
            if(vector.size<0 || vector.size>4096 || vector.capacity<vector.size
                || vector.capacity>8192
                || !address(vector.data,std::size_t(vector.size)*sizeof(values[i]))) return {};
            tails[i]=vector.data+std::size_t(vector.size-1)*sizeof(values[i]);
            if(!reader.copy(tails[i],&values[i],sizeof(values[i]))) return {};
        }
        if(!valid(values[i])) return {};
    }
    for(std::size_t i=0;i<values.size();++i) if(tails[i]) {
        foundation::vec3 after_value{};
        if(!reader.copy(tails[i],&after_value,sizeof(after_value)) || values[i]!=after_value) return {};
    }
    // Validate both tracks together, including their base epochs and in-place cache rewrites.
    if(!reader.copy(services+start,after.data(),after.size()) || before!=after) return {};
    std::uintptr_t after_services{};
    if(!reader.copy(pawn+services_offset,&after_services,sizeof(after_services))
        || services!=after_services) return {};
    foundation::vec3 result{};
    for(const auto& value:values) result+=value;
    result*=2.0f;
    return valid(result)?std::optional{result}:std::nullopt;
}
} // namespace simulation
