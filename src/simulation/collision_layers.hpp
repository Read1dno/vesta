#pragma once
#include <cstdint>
#include <string_view>

namespace game::collision_detail {
inline constexpr std::uint32_t pass_bullets_layer = 1u << 13;
[[nodiscard]] constexpr std::uint32_t bullet_interaction_layer(std::string_view name) noexcept
{
    constexpr std::string_view expected = "passbullets";
    if (name.size() != expected.size()) return 0;
    for (std::size_t i=0; i<name.size(); ++i) {
        auto c=name[i];
        if (c>='A' && c<='Z') c=static_cast<char>(c-'A'+'a');
        if (c!=expected[i]) return 0;
    }
    return pass_bullets_layer;
}
} // namespace game::collision_detail
