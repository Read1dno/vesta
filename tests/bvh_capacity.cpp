#include <simulation/bvh_capacity.hpp>
#include <cstdio>

constexpr std::size_t actual_nodes(std::size_t n)
{
	if (!n)
		return 0;
	return n <= 8 ? 1 : 1 + actual_nodes(n / 2) + actual_nodes(n - n / 2);
}

int main()
{
	for (std::size_t n = 0; n <= 10000; ++n)
		if (actual_nodes(n) > game::detail::bvh_node_capacity(n, 8))
			return 1;
	constexpr auto n = 1048576u;
	const auto capacity = game::detail::bvh_node_capacity(n, 8);
	if (capacity != 262143 || actual_nodes(n) != capacity)
		return 2;
	std::printf("PASS BVH capacity: triangles=%u old_nodes=%u new_nodes=%zu\n", n, 2 * n, capacity);
}
