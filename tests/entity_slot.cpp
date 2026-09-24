#include <core/memory/entity_slot.hpp>
#include "test_support.hpp"
#include <unordered_map>
int main() {
    std::unordered_map<std::uintptr_t,std::uint8_t> memory;
    auto put=[&](auto address,auto value) {
        const auto* bytes=reinterpret_cast<const std::uint8_t*>(&value);
        for (std::size_t i=0;i<sizeof(value);++i) memory[address+i]=bytes[i];
    };
    auto read=[&](auto address,void* out,auto size) {
        for (std::size_t i=0;i<size;++i) if (!memory.contains(address+i)) return false;
        for (std::size_t i=0;i<size;++i) static_cast<std::uint8_t*>(out)[i]=memory[address+i];
        return true;
    };
    constexpr std::uintptr_t list=0x10000,chunk=0x20000,entity=0x30000;
    put(list+0x10,chunk);
    for (unsigned i=0;i<0x14;++i) memory[chunk+i]=0;
    put(chunk,entity);
    VESTA_CHECK(game::read_entity_slot(list,0,true,read)==entity);
    VESTA_CHECK(!game::read_entity_slot(list,0xffffffffu,true,read));
    VESTA_CHECK(!game::read_entity_slot(list,0xfffffffeu,true,read));
    VESTA_CHECK(!game::read_entity_slot(list,0x8000u,true,read));
    put(chunk+0x10,0xffffffffu);
    VESTA_CHECK(!game::read_entity_slot(list,0x8000u,true,read));
    put(chunk+0x10,0x8000u);
    VESTA_CHECK(game::read_entity_slot(list,0x8000u,true,read)==entity);
    VESTA_CHECK(!game::read_entity_slot(list,0,true,read));
    VESTA_CHECK(game::read_entity_slot(list,0,false,read)==entity);
    memory.erase(chunk+0x10);
    VESTA_CHECK(!game::read_entity_slot(list,0x8000u,true,read));
    VESTA_CHECK(!game::read_entity_slot(0,0,true,read));
    VESTA_CHECK(!game::read_entity_slot(list+8,0,true,read));
    std::cout<<"entity_slot: zero handle, serial reuse, invalid handles, read failures PASS\n";
}
