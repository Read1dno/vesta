#pragma once

namespace foundation {

	class frame_limiter
	{
	public:
		explicit frame_limiter( std::uint32_t frames_per_second ) noexcept;
		void wait( ) noexcept;
		void set_rate( std::uint32_t frames_per_second ) noexcept;

	private:
		using clock = std::chrono::steady_clock;
		clock::duration m_period{};
		clock::time_point m_deadline{};
		std::uint32_t m_rate{};
	};

}
