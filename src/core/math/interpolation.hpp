#pragma once

namespace motion {

	enum class curve : std::uint8_t
	{
		linear,
		accelerate,
		decelerate,
		smooth
	};

	class scalar_transition
	{
	public:
		void start( float from, float to, float duration,
			curve shape = curve::decelerate ) noexcept;
		void update( ) noexcept;
		void reset( ) noexcept;

		[[nodiscard]] float value( ) const noexcept { return m_value; }
		[[nodiscard]] bool finished( ) const noexcept { return !m_active; }

	private:
		[[nodiscard]] float remap( float progress ) const noexcept;

		float m_begin{};
		float m_end{};
		float m_value{};
		float m_duration{};
		float m_elapsed{};
		curve m_curve{ curve::linear };
		bool m_active{};
	};

	class damped_value
	{
	public:
		void set_target( float target ) noexcept { m_target = target; }
		void update( ) noexcept;
		void snap( float value ) noexcept;
		void set_stiffness( float value ) noexcept { m_stiffness = value; }
		void set_damping( float value ) noexcept { m_damping = value; }

		[[nodiscard]] float value( ) const noexcept { return m_value; }
		[[nodiscard]] bool settled( ) const noexcept;

	private:
		float m_value{};
		float m_velocity{};
		float m_target{};
		float m_stiffness{ 200.0f };
		float m_damping{ 20.0f };
	};

}
