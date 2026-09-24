#include <bit>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <limits>
#include <random>
#include <render/depth_bounds.hpp>
#include <string_view>

struct vec
{
	float x{}, y{}, z{};
};
struct bounds
{
	vec mins, maxs;
};
struct chunk
{
	bounds bounds;
};
struct volume
{
	float min_x, min_y, max_x, max_y, max_depth;
};
using matrix = std::array<std::array<float, 4>, 4>;
render::projected_depth_bounds reference(const bounds &b, const matrix &m)
{
	render::projected_depth_bounds p;
	for (int corner = 0; corner < 8; ++corner)
	{
		const vec v{(corner & 1) ? b.maxs.x : b.mins.x, (corner & 2) ? b.maxs.y : b.mins.y,
		            (corner & 4) ? b.maxs.z : b.mins.z};
		const auto x = m[0][0] * v.x + m[0][1] * v.y + m[0][2] * v.z + m[0][3];
		const auto y = m[1][0] * v.x + m[1][1] * v.y + m[1][2] * v.z + m[1][3];
		const auto z = m[2][0] * v.x + m[2][1] * v.y + m[2][2] * v.z + m[2][3];
		const auto w = m[3][0] * v.x + m[3][1] * v.y + m[3][2] * v.z + m[3][3];
		if (w <= 0.001f)
		{
			p.crosses_near = true;
			continue;
		}
		const auto inverse = 1.0f / w;
		p.min_x = std::min(p.min_x, x * inverse);
		p.min_y = std::min(p.min_y, y * inverse);
		p.max_x = std::max(p.max_x, x * inverse);
		p.max_y = std::max(p.max_y, y * inverse);
		p.min_depth = std::min(p.min_depth, z * inverse);
		p.valid = true;
	}
	if (p.valid && p.crosses_near)
	{
		p.min_x = p.min_y = -1;
		p.max_x = p.max_y = 1;
		p.min_depth = 0;
	}
	return p;
}
bool equal(const render::projected_depth_bounds &a, const render::projected_depth_bounds &b)
{
	return a.min_x == b.min_x && a.min_y == b.min_y && a.max_x == b.max_x && a.max_y == b.max_y &&
	       a.min_depth == b.min_depth && a.valid == b.valid && a.crosses_near == b.crosses_near;
}
int main(int argc, char **argv)
{
	std::mt19937 random(67217);
	std::uniform_real_distribution<float> xyz(-4000, 4000), size(0, 120), value(-2, 2);
	auto next_bounds = [&] {
		bounds b{{xyz(random), xyz(random), xyz(random)}, {}};
		b.maxs = {b.mins.x + size(random), b.mins.y + size(random), b.mins.z + size(random)};
		return b;
	};
	matrix m{};
	for (auto &row : m)
		for (auto &f : row)
			f = value(random);
	if (argc > 1)
	{
		const bool baseline = std::string_view(argv[1]) == "--baseline";
		const bool moving = argc > 2 && std::string_view(argv[2]) == "moving";
		std::vector<chunk> chunks;
		for (int i = 0; i < 10000; ++i)
			chunks.push_back({next_bounds()});
		std::vector<volume> volumes{{-0.2f, -0.4f, 0.1f, 0.2f, 0.7f}, {0.2f, 0.1f, 0.3f, 0.4f, 0.8f}};
		render::depth_projection_cache cache;
		std::uint64_t checksum = 0;
		const auto start = std::chrono::steady_clock::now();
		for (int frame = 0; frame < 200; ++frame)
		{
			if (moving)
				m[0][3] += 0.125f;
			if (baseline)
				for (std::size_t i = 0; i < chunks.size(); ++i)
				{
					if (render::relevant_depth_bounds(reference(chunks[i].bounds, m), volumes))
						checksum += i;
				}
			else
			{
				cache.update(m, chunks);
				cache.select(volumes, [&](auto i) { checksum += i; });
			}
		}
		const auto us =
		    std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - start)
		        .count();
		std::printf("%s %s: chunks=10000 frames=200 checksum=%llu elapsed_us=%lld\n",
		            baseline ? "BASELINE" : "MODIFIED", moving ? "moving" : "static",
		            static_cast<unsigned long long>(checksum), static_cast<long long>(us));
		return 0;
	}
	for (int i = 0; i < 100000; ++i)
	{
		auto b = next_bounds();
		for (auto &row : m)
			for (auto &f : row)
				f = value(random);
		if (!equal(reference(b, m), render::project_depth_bounds(b, m)))
		{
			std::printf("FAIL projection case %d\n", i);
			return 1;
		}
	}
	for (float w : {0.0f, -0.0f, 0.001f, std::nextafter(0.001f, 1.0f), -1.0f, 1.0f,
	                std::numeric_limits<float>::infinity(), std::numeric_limits<float>::quiet_NaN()})
	{
		m = {};
		m[0][0] = m[1][1] = m[2][2] = 1;
		m[3][3] = w;
		const auto b = next_bounds();
		if (!equal(reference(b, m), render::project_depth_bounds(b, m)))
			return 2;
	}
	for (int round = 0; round < 500; ++round)
	{
		const auto count = round % 3 ? 1025 : 0;
		std::vector<chunk> chunks;
		for (int i = 0; i < count; ++i)
			chunks.push_back({next_bounds()});
		for (auto &row : m)
			for (auto &f : row)
				f = value(random);
		std::vector<volume> volumes;
		for (int i = 0; i < round % 10; ++i)
		{
			const auto x = value(random), y = value(random);
			volumes.push_back({x, y, x + 0.4f, y + 0.6f, 0.8f});
		}
		render::depth_projection_cache cache;
		if (!cache.update(m, chunks) || cache.update(m, chunks))
			return 3;
		std::vector<std::size_t> expected, actual;
		for (std::size_t i = 0; i < chunks.size(); ++i)
			if (render::relevant_depth_bounds(reference(chunks[i].bounds, m), volumes))
				expected.push_back(i);
		cache.select(volumes, [&](auto i) { actual.push_back(i); });
		if (actual != expected)
			return 4;
		m[0][0] = std::nextafter(m[0][0], 10.0f);
		if (!cache.update(m, chunks))
			return 5;
		cache = {};
		if (!cache.update(m, chunks))
			return 6;
	}
	std::puts("PASS depth projection: 100000 exact scalar/SIMD cases; 500 ordered "
	          "selections; near-plane/nonfinite/matrix/revision invalidation");
}
