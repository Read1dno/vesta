#pragma once

#include <bit>
#include <cstddef>

namespace game::detail
{

// Median splitting produces at most this many leaves of at most leaf_size items.
[[nodiscard]] constexpr std::size_t bvh_node_capacity(std::size_t items, std::size_t leaf_size)
{
	if (items == 0 || leaf_size == 0)
		return 0;
	const auto leaves = items / leaf_size + (items % leaf_size != 0);
	return std::bit_ceil(leaves) * 2 - 1;
}

} // namespace game::detail
