#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <numbers>
#include <optional>
#include <span>
#include <string_view>
#include <unordered_map>
#include <core/math/vector.hpp>
constexpr std::string_view operator ""_id(const char* p,std::size_t n){return {p,n};}
#define SCHEMA(c,n) 0x100
namespace fixture {
inline std::unordered_map<std::uintptr_t,std::byte> memory;
inline unsigned calls{},fail_call{};
inline std::uintptr_t mutate_address{};
inline unsigned mutate_call{};
inline std::byte mutate_value{};
template<class T> void put(std::uintptr_t address,const T& value) {
 auto p=reinterpret_cast<const std::byte*>(&value);
 for(std::size_t i=0;i<sizeof(value);++i)memory[address+i]=p[i];
}
}
namespace app {
struct reader {
 bool copy(std::uintptr_t address,void* output,std::size_t size) const {
  ++fixture::calls;
  if(fixture::mutate_call==fixture::calls)fixture::memory[fixture::mutate_address]=fixture::mutate_value;
  if(fixture::fail_call==fixture::calls)return false;
  for(std::size_t i=0;i<size;++i)if(!fixture::memory.contains(address+i))return false;
  for(std::size_t i=0;i<size;++i)static_cast<std::byte*>(output)[i]=fixture::memory.at(address+i);
  return true;
 }
 template<class T> T load(std::uintptr_t address)const{T value{};copy(address,&value,sizeof(value));return value;}
};
struct context_t{reader process;};
inline context_t& context(){static context_t value;return value;}
}
