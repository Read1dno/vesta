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
    constexpr std::uintptr_t list2=0x40000,chunk2=0x50000;
    put(list2+0x10,chunk2);
    for (unsigned i=0;i<65*112;++i) memory[chunk2+i]=0;
    put(chunk2+112,entity);
    put(chunk2+4*112,entity+0x1000);
    put(chunk2+64*112,entity+0x2000);
    std::array<std::uintptr_t,65> batch{};
    unsigned reads{};
    auto counting_read=[&](auto address,void* out,auto size){++reads;return read(address,out,size);};
    VESTA_CHECK(game::read_player_controller_slots(list2,batch,counting_read));
    VESTA_CHECK(reads==2 && batch[0]==0 && batch[1]==entity
        && batch[4]==entity+0x1000 && batch[64]==entity+0x2000
        && batch[2]==0 && batch[63]==0);
    VESTA_CHECK(batch[1]==game::read_entity_slot(list2,1,false,read));
    VESTA_CHECK(batch[64]==game::read_entity_slot(list2,64,false,read));
    memory.erase(chunk2+64*112);
    VESTA_CHECK(!game::read_player_controller_slots(list2,batch,read));
    VESTA_CHECK(batch[1]==0 && batch[64]==0);
    VESTA_CHECK(!game::read_player_controller_slots(0,batch,read));
    std::cout<<"entity_slot: zero handle, serial reuse, invalid handles, batch read PASS\n";
}
