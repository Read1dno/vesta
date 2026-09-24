#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <format>
#include <mutex>
#include <numbers>
#include <string>
#include <string_view>
#include <thread>

namespace fixture {
enum class sample { stable, torn_time, torn_wat, torn_punch, failed_read, nan_wat, nan_punch, missing_wat, zero_time, boundary };
inline sample mode{};
inline std::filesystem::path executable;
inline int schema(std::string_view field) {
    if (field == "m_fLastShotTime") return 16;
    if (field == "m_flWatTickOffset") return mode == sample::missing_wat ? 0 : 20;
    return 0;
}
}
constexpr std::string_view operator""_id(const char* text, std::size_t size) { return {text,size}; }
#define SCHEMA(type,field) fixture::schema(field)
namespace app {
struct reader {
    unsigned time_reads{}, wat_reads{}, punch_reads{};
    bool copy(std::uintptr_t address, void* out, std::size_t size) {
        using fixture::sample;
        const auto mode=fixture::mode;
        if (mode==sample::failed_read) return false;
        if (address==0x20010 && size==4) {
            float value=mode==sample::zero_time ? 0 : mode==sample::boundary ? 100.0f/64 : 100.5f/64;
            if (++time_reads==2 && mode==sample::torn_time) value+=1.0f/64;
            std::memcpy(out,&value,size); return true;
        }
        if (address==0x20014 && size==4) {
            float value=mode==sample::boundary ? -1.0f : -1.25f;
            if (++wat_reads==2 && mode==sample::torn_wat) value+=1;
            if (mode==sample::nan_wat) value=NAN;
            std::memcpy(out,&value,size); return true;
        }
        if (address==0x20020 && size==12) {
            float value[3]{};
            if (++punch_reads==2 && mode==sample::torn_punch) value[0]=1;
            if (mode==sample::nan_punch) value[0]=NAN;
            std::memcpy(out,value,size); return true;
        }
        return false;
    }
};
struct context_t { reader process; };
inline context_t& context() { static context_t c; return c; }
}
inline DWORD fixture_module(HMODULE, wchar_t* out, DWORD capacity) {
    const auto value=fixture::executable.wstring();
    wcscpy_s(out,capacity,value.c_str());
    return static_cast<DWORD>(value.size());
}
#define GetModuleFileNameW fixture_module
