#pragma once

namespace foundation {

	class source_random
	{
	public:
		void seed( int value ) noexcept;
		[[nodiscard]] float uniform( float minimum = 0.0f,
			float maximum = 1.0f ) noexcept;

	private:
		[[nodiscard]] int next_integer( ) noexcept;
		[[nodiscard]] static int advance( int value ) noexcept;

		int m_state{};
		int m_shuffle{};
		std::array<int, 32> m_pool{};
		bool m_ready{};
	};

	[[nodiscard]] std::uint32_t sha1_first_word(
		std::span<const std::byte> message ) noexcept;

}
