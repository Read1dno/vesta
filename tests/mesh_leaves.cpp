#include <simulation/mesh_leaves.hpp>
#include <cstdio>
#include <cstdlib>
#include <new>

static std::size_t allocation_budget = SIZE_MAX;
void *operator new(std::size_t size)
{
	if (size > allocation_budget)
		throw std::bad_alloc();
	if (allocation_budget != SIZE_MAX)
		allocation_budget -= size;
	if (void *ptr = std::malloc(size ? size : 1))
		return ptr;
	throw std::bad_alloc();
}
void operator delete(void *ptr) noexcept
{
	std::free(ptr);
}
void operator delete(void *ptr, std::size_t) noexcept
{
	std::free(ptr);
}

int main()
{
	using namespace game::detail;
	const auto leaf = [](std::uint32_t first, std::uint32_t count) {
		packed_mesh_node node{};
		node.packed0 = 0xc0000000u | count;
		node.packed1 = first;
		return node;
	};
	std::vector<packed_mesh_node> nodes(3);
	nodes[0].packed0 = 2;
	nodes[1] = leaf(0, 2);
	nodes[2] = leaf(2, 3);
	const auto valid = discover_mesh_leaves(nodes);
	if (!valid || valid->spans.size() != 2 || valid->past_last_triangle != 5)
		return 2;

	// Both branches alias the same child: the old walk expands 2^39 paths.
	nodes.assign(40, {});
	for (auto &node : nodes)
		node.packed0 = 1;
	nodes.back() = leaf(0, 1);
	allocation_budget = 4 * 1024 * 1024;
	try
	{
		if (discover_mesh_leaves(nodes))
		{
			std::puts("FAIL duplicate child accepted");
			return 3;
		}
	}
	catch (const std::bad_alloc &)
	{
		allocation_budget = SIZE_MAX;
		std::puts("FAIL duplicate-child mesh exhausted 4 MiB allocation budget");
		return 1;
	}
	allocation_budget = SIZE_MAX;
	nodes = {leaf(UINT32_MAX - 1, 4)};
	if (discover_mesh_leaves(nodes))
		return 4;
	nodes.assign(3, {});
	nodes[0].packed0 = 99;
	if (discover_mesh_leaves(nodes))
		return 5;
	nodes[0].packed0 = 2;
	nodes[1] = leaf(0, 3);
	nodes[2] = leaf(2, 1);
	if (discover_mesh_leaves(nodes))
		return 6;
#ifndef VESTA_BASELINE_TEST
	std::stop_source cancelled;
	cancelled.request_stop();
	if (discover_mesh_leaves(nodes, cancelled.get_token()))
		return 7;
	traversal_guard outer(3);
	if (!outer.enter(0) || outer.enter(0) || outer.enter(3))
		return 8;
#endif
	if (discover_mesh_leaves({}))
		return 9;
	std::puts("PASS mesh bounds, duplicate children, overflow, overlaps, cancellation and cycles");
}
