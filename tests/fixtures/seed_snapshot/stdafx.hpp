#pragma once
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <limits>
#include <numbers>
#include <optional>
#include <shared_mutex>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <vector>
#include <core/math/vector.hpp>
constexpr std::string_view operator ""_id(const char* p, std::size_t n) { return {p,n}; }
inline int schema(std::string_view name) {
    if(name=="m_AttributeManager")return 0x2000;
    if(name=="m_Item")return 0x200;
    if(name=="m_iItemDefinitionIndex")return 0x40;
    static std::unordered_map<std::string_view,int> fields;
    auto [it, added] = fields.try_emplace(name, 32 + int(fields.size()) * 32);
    return it->second;
}
#define SCHEMA(c,n) schema(n)
#define CONVAR(n) (n)
namespace fixture {
inline std::unordered_map<std::uintptr_t,std::byte> memory;
inline std::unordered_map<std::uintptr_t,unsigned> counts;
inline unsigned calls{}, failure_call{}, change_on{};
inline std::uintptr_t changed_address{};
inline std::vector<std::byte> changed_value;
inline std::string_view failed_variable;
inline std::unordered_map<std::string_view,double> variables;
template<class T> void put(std::uintptr_t address, const T& value) {
    const auto p = reinterpret_cast<const std::byte*>(&value);
    for (std::size_t i=0;i<sizeof(T);++i) memory[address+i]=p[i];
}
template<class T> void change(std::uintptr_t address, T value, unsigned occurrence) {
    changed_address=address;change_on=occurrence;changed_value.resize(sizeof(T));
    std::memcpy(changed_value.data(),&value,sizeof(T));
}
inline void clear_reads() { counts.clear();calls=0;failure_call=0;change_on=0;failed_variable={};variables.clear(); }
}
namespace app {
struct reader {
    bool copy(std::uintptr_t address, void* data, std::size_t size) const {
        const auto occurrence=++fixture::counts[address];
        if (++fixture::calls==fixture::failure_call) return false;
        if (address==fixture::changed_address && fixture::change_on
            && occurrence>=fixture::change_on && size==fixture::changed_value.size()) {
            std::memcpy(data,fixture::changed_value.data(),size);return true;
        }
        for(std::size_t i=0;i<size;++i) if(!fixture::memory.contains(address+i))return false;
        for(std::size_t i=0;i<size;++i)static_cast<std::byte*>(data)[i]=fixture::memory.at(address+i);
        return true;
    }
};
struct context_t { reader process; };
inline context_t& context() {static context_t c;return c;}
}
namespace game {
struct player_snapshot;
struct skeleton_reader { struct data; };
struct directory {
    std::uintptr_t lookup(std::uint32_t h) const {return h==17 ? 0x30000 : h==0 ? 0x90000 : 0;}
};
inline directory& entity_index(){static directory d;return d;}
struct variable_store {
    template<class T> bool try_get(std::string_view name,T& value) const {
        if(name==fixture::failed_variable)return false;
        const bool boolean = name=="weapon_accuracy_nospread" || name=="sv_strafing_inaccuracy_enabled";
        if (boolean != std::is_same_v<T,bool>) return false;
        if (auto it=fixture::variables.find(name);it!=fixture::variables.end()) {
            value=static_cast<T>(it->second);return true;
        }
        const double v=name=="sv_jump_impulse" ? 301.993377
            : name=="weapon_air_spread_scale" ? 1.0
            : name=="sv_strafing_inaccuracy_bias" ? 0.5
            : name=="sv_strafing_inaccuracy_scale" ? 0.1 : 0.0;
        value=static_cast<T>(v);return true;
    }
};
inline variable_store& variables(){static variable_store v;return v;}
}
