#pragma once
#include <array>
#include <cstdint>
#include <cstring>

namespace game {
template<class Reader>
[[nodiscard]] std::uintptr_t read_entity_slot(std::uintptr_t list,
    std::uint32_t handle, bool validate_serial, Reader&& read)
{
    if (!list || handle==0xffffffffu || handle==0xfffffffeu) return 0;
    std::uintptr_t chunk{};
    if (!read(list+0x10+((handle & 0x7fffu)>>9)*8,&chunk,sizeof(chunk)) || !chunk) return 0;
    const auto address=chunk+(handle & 0x1ffu)*112;
    std::array<std::uint8_t,0x14> slot{};
    if (!read(address,slot.data(),validate_serial ? slot.size() : sizeof(std::uintptr_t))) return 0;
    std::uintptr_t entity{};
    std::uint32_t resident{};
    std::memcpy(&entity,slot.data(),sizeof(entity));
    std::memcpy(&resident,slot.data()+0x10,sizeof(resident));
    return entity>=0x10000 && (!validate_serial || resident==handle) ? entity : 0;
}
}
