#pragma once

#include <algorithm>
#include <cstdint>
#include <optional>
#include <span>
#include <stop_token>
#include <vector>

namespace game::detail
{

struct packed_mesh_node
{
	float min[3]{};
	std::uint32_t packed0{};
	float max[3]{};
	std::uint32_t packed1{};

	[[nodiscard]] std::uint32_t type() const
	{
		return packed0 >> 30;
	}
	[[nodiscard]] std::uint32_t payload() const
	{
		return packed0 & 0x3fffffffu;
	}
};

struct triangle_span
{
	std::uint32_t first{}, count{};
};
struct mesh_leaf_map
{
	std::vector<triangle_span> spans{};
	std::uint32_t first_triangle{UINT32_MAX};
	std::uint32_t past_last_triangle{};
};

inline constexpr std::size_t remote_array_budget = 64 * 1024 * 1024;

// A tree node may have only one parent, including in a torn remote snapshot.
class traversal_guard
{
  public:
	explicit traversal_guard(std::size_t count) : visited_(count)
	{
	}
	[[nodiscard]] bool enter(std::size_t index)
	{
		if (index >= visited_.size() || visited_[index])
			return false;
		visited_[index] = 1;
		return true;
	}

  private:
	std::vector<std::uint8_t> visited_;
};

[[nodiscard]] inline std::optional<mesh_leaf_map> discover_mesh_leaves(
    std::span<const packed_mesh_node> nodes, std::stop_token stop = {})
{
	if (nodes.empty() || nodes.size_bytes() > remote_array_budget || stop.stop_requested())
		return std::nullopt;
	mesh_leaf_map result;
	traversal_guard visited(nodes.size());
	std::vector<std::uint32_t> pending{0};
	std::size_t steps{};
	while (!pending.empty())
	{
		if ((steps++ & 255u) == 0 && stop.stop_requested())
			return std::nullopt;
		const auto cursor = pending.back();
		pending.pop_back();
		if (!visited.enter(cursor))
			return std::nullopt;
		const auto &node = nodes[cursor];
		const auto payload = node.payload();
		if (node.type() == 3)
		{
			if (payload == 0 || payload >= 0x1000000u || payload > UINT32_MAX - node.packed1)
				return std::nullopt;
			result.spans.push_back({node.packed1, payload});
			result.first_triangle = std::min(result.first_triangle, node.packed1);
			result.past_last_triangle = std::max(result.past_last_triangle, node.packed1 + payload);
		}
		else
		{
			if (payload < 2 || payload >= nodes.size() - cursor)
				return std::nullopt;
			pending.push_back(cursor + payload);
			pending.push_back(cursor + 1);
		}
	}
	if (result.spans.empty())
		return std::nullopt;
	std::ranges::sort(result.spans, {}, &triangle_span::first);
	for (std::size_t i = 1; i < result.spans.size(); ++i)
		if (result.spans[i - 1].first + result.spans[i - 1].count > result.spans[i].first)
			return std::nullopt;
	return result;
}

} // namespace game::detail
