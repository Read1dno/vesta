#pragma once
#include <bit>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

namespace game::binding_detail {
[[nodiscard]] constexpr std::optional<std::ptrdiff_t> select_layout_bias(
    std::span<const std::uint16_t> matches, std::ptrdiff_t minimum_bias) noexcept
{
    int best{}, runner_up{};
    std::ptrdiff_t candidate{};
    for (std::size_t i = 0; i < matches.size(); ++i)
    {
        const auto score = std::popcount(matches[i]);
        if (score > best) { runner_up = best; best = score; candidate = minimum_bias + i; }
        else if (score > runner_up) runner_up = score;
    }
    return best >= 4 && best > runner_up ? std::optional{candidate} : std::nullopt;
}
}
