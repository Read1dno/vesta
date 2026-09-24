#pragma once

#include <algorithm>
#include <array>
#include <cstring>
#include <immintrin.h>
#include <vector>

namespace render
{
struct projected_depth_bounds
{
	float min_x{1e12f}, min_y{1e12f};
	float max_x{-1e12f}, max_y{-1e12f};
	float min_depth{1.0f};
	bool valid{};
	bool crosses_near{};
};

template <class Bounds, class Matrix>
[[nodiscard]] projected_depth_bounds project_depth_bounds(const Bounds &bounds, const Matrix &matrix)
{
	projected_depth_bounds out;
	const auto x = _mm_setr_ps(bounds.mins.x, bounds.maxs.x, bounds.mins.x, bounds.maxs.x);
	const auto y = _mm_setr_ps(bounds.mins.y, bounds.mins.y, bounds.maxs.y, bounds.maxs.y);
	for (int half = 0; half < 2; ++half)
	{
		const auto z = _mm_set1_ps(half ? bounds.maxs.z : bounds.mins.z);
		const auto transform = [&](int row) {
			return _mm_add_ps(_mm_add_ps(_mm_add_ps(_mm_mul_ps(_mm_set1_ps(matrix[row][0]), x),
			                                        _mm_mul_ps(_mm_set1_ps(matrix[row][1]), y)),
			                             _mm_mul_ps(_mm_set1_ps(matrix[row][2]), z)),
			                  _mm_set1_ps(matrix[row][3]));
		};
		const auto w = transform(3);
		const auto rejected = _mm_cmple_ps(w, _mm_set1_ps(0.001f));
		if (_mm_movemask_ps(rejected) == 15)
		{
			out.crosses_near = true;
			continue;
		}
		const auto divisor = _mm_or_ps(_mm_and_ps(rejected, _mm_set1_ps(1.0f)), _mm_andnot_ps(rejected, w));
		const auto inverse = _mm_div_ps(_mm_set1_ps(1.0f), divisor);
		std::array<float, 4> clip_w, px, py, pz;
		_mm_storeu_ps(clip_w.data(), w);
		_mm_storeu_ps(px.data(), _mm_mul_ps(transform(0), inverse));
		_mm_storeu_ps(py.data(), _mm_mul_ps(transform(1), inverse));
		_mm_storeu_ps(pz.data(), _mm_mul_ps(transform(2), inverse));
		for (int lane = 0; lane < 4; ++lane)
		{
			if (clip_w[lane] <= 0.001f)
			{
				out.crosses_near = true;
				continue;
			}
			out.min_x = std::min(out.min_x, px[lane]);
			out.min_y = std::min(out.min_y, py[lane]);
			out.max_x = std::max(out.max_x, px[lane]);
			out.max_y = std::max(out.max_y, py[lane]);
			out.min_depth = std::min(out.min_depth, pz[lane]);
			out.valid = true;
		}
	}
	if (out.valid && out.crosses_near)
	{
		out.min_x = out.min_y = -1.0f;
		out.max_x = out.max_y = 1.0f;
		out.min_depth = 0.0f;
	}
	return out;
}

template <class Volumes>
[[nodiscard]] bool relevant_depth_bounds(const projected_depth_bounds &projected, const Volumes &volumes)
{
	return projected.valid && std::any_of(volumes.begin(), volumes.end(), [&](const auto &volume) {
		       return projected.min_depth <= volume.max_depth + 0.0005f && projected.min_x <= volume.max_x &&
		              projected.max_x >= volume.min_x && projected.min_y <= volume.max_y &&
		              projected.max_y >= volume.min_y;
	       });
}

// Owned by one immutable geometry revision; no camera tolerance or time-based
// reuse.
class depth_projection_cache
{
	static constexpr std::size_t block_size = 32;
	std::array<std::array<float, 4>, 4> m_matrix{};
	bool m_valid{};
	std::vector<projected_depth_bounds> m_bounds;
	std::vector<projected_depth_bounds> m_blocks;

  public:
	template <class Matrix, class Chunks> bool update(const Matrix &matrix, const Chunks &chunks)
	{
		decltype(m_matrix) current;
		for (int row = 0; row < 4; ++row)
			for (int column = 0; column < 4; ++column)
				current[row][column] = matrix[row][column];
		if (m_valid && m_bounds.size() == chunks.size() &&
		    std::memcmp(current.data(), m_matrix.data(), sizeof(current)) == 0)
			return false;
		m_valid = false;
		m_bounds.resize(chunks.size());
		m_blocks.assign((chunks.size() + block_size - 1) / block_size, {});
		for (std::size_t i = 0; i < chunks.size(); ++i)
		{
			const auto p = project_depth_bounds(chunks[i].bounds, matrix);
			m_bounds[i] = p;
			if (!p.valid)
				continue;
			auto &block = m_blocks[i / block_size];
			block.min_x = std::min(block.min_x, p.min_x);
			block.min_y = std::min(block.min_y, p.min_y);
			block.max_x = std::max(block.max_x, p.max_x);
			block.max_y = std::max(block.max_y, p.max_y);
			block.min_depth = std::min(block.min_depth, p.min_depth);
			block.valid = true;
		}
		m_matrix = current;
		m_valid = true;
		return true;
	}
	template <class Volumes, class Emit> void select(const Volumes &volumes, Emit &&emit) const
	{
		for (std::size_t block = 0; block < m_blocks.size(); ++block)
		{
			if (!relevant_depth_bounds(m_blocks[block], volumes))
				continue;
			const auto end = std::min((block + 1) * block_size, m_bounds.size());
			for (std::size_t i = block * block_size; i < end; ++i)
				if (relevant_depth_bounds(m_bounds[i], volumes))
					emit(i);
		}
	}
};
} // namespace render
