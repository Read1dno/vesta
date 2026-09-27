#include <numbers>
#include <simulation/native_velocity.hpp>
#include <array>
#include <cstring>
#include <iostream>
#include <limits>
#include <unordered_map>

namespace {
constexpr std::uintptr_t pawn = 0x10000, node = 0x20000;
constexpr simulation::native_velocity::offsets offsets{0x374, 0x3f8, 0x430, 0x330, 0x38};
struct reader {
    std::unordered_map<std::uintptr_t, std::byte> memory;
    mutable int flag_reads{};
    bool flip_flag{}, fail_raw{};
    template<class T> void put(std::uintptr_t address, const T& value) {
        const auto* bytes = reinterpret_cast<const std::byte*>(&value);
        for (std::size_t i = 0; i < sizeof(T); ++i) memory[address+i] = bytes[i];
    }
    bool copy(std::uintptr_t address, void* target, std::size_t size) const {
        if (address == pawn+offsets.network && fail_raw) return false;
        if (address == pawn+offsets.eflags && flip_flag && ++flag_reads == 2) {
            const std::uint32_t clean{};
            std::memcpy(target, &clean, size);
            return true;
        }
        for (std::size_t i = 0; i < size; ++i) {
            const auto found = memory.find(address+i);
            if (found == memory.end()) return false;
            static_cast<std::byte*>(target)[i] = found->second;
        }
        return true;
    }
};
reader fixture(std::uint32_t flags) {
    reader value;
    value.put(pawn+offsets.eflags, flags);
    value.put(pawn+offsets.absolute, foundation::vec3{0,0,0});
    value.put(pawn+offsets.network, foundation::vec3{250,0,300});
    value.put(pawn+offsets.scene_node, node);
    value.put(node+offsets.parent, std::uintptr_t{});
    return value;
}
}
int main() {
    unsigned cases{}, failed{};
    const auto check = [&](const char* name, bool actual) {
        ++cases;
        if (!actual) { ++failed; std::cout << "FAIL " << name << '\n'; }
    };
    foundation::vec3 result{};
    auto clean = fixture(0);
    check("clean_uses_cached", simulation::native_velocity::read(clean,pawn,offsets,result)
        && result == foundation::vec3{0,0,0});
    auto dirty = fixture(0x1000);
    check("dirty_uses_network", simulation::native_velocity::read(dirty,pawn,offsets,result)
        && result == foundation::vec3{250,0,300});
    dirty.put(node+offsets.parent, std::uintptr_t{0x30000});
    check("parent_requires_transform", !simulation::native_velocity::read(dirty,pawn,offsets,result));
    dirty = fixture(0x1000); dirty.flip_flag = true;
    check("dirty_transition_retries", !simulation::native_velocity::read(dirty,pawn,offsets,result));
    dirty = fixture(0x1000); dirty.fail_raw = true;
    check("failed_raw_read", !simulation::native_velocity::read(dirty,pawn,offsets,result));
    dirty = fixture(0x1000);
    dirty.put(pawn+offsets.network, foundation::vec3{std::numeric_limits<float>::quiet_NaN(),0,0});
    check("nonfinite_raw", !simulation::native_velocity::read(dirty,pawn,offsets,result));
    clean = fixture(0);
    check("invalid_offset", !simulation::native_velocity::read(clean,pawn,{},result));
    check("invalid_pawn", !simulation::native_velocity::read(clean,0,offsets,result));
    std::cout << "native_velocity cases=" << cases << " failed=" << failed << '\n';
    return failed ? 1 : 0;
}
