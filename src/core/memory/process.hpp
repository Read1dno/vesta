#pragma once

namespace platform::windows {

	class process_session
	{
	public:
		process_session( ) = default;
		~process_session( );

		process_session( const process_session& ) = delete;
		process_session& operator=( const process_session& ) = delete;

		[[nodiscard]] bool attach( std::wstring_view executable );
		void detach( ) noexcept;

		[[nodiscard]] bool copy( std::uintptr_t address, void* destination,
			std::size_t bytes ) const noexcept;

		template <typename value_t>
		[[nodiscard]] value_t load( std::uintptr_t address,
			value_t fallback = {} ) const noexcept
		{
			value_t value{};
			return this->copy( address, &value, sizeof( value ) ) ? value : fallback;
		}

		template <typename value_t = std::uintptr_t>
		[[nodiscard]] value_t decode_rip( std::uintptr_t instruction,
			std::int32_t displacement_offset = 3,
			std::int32_t instruction_size = 7 ) const noexcept
		{
			const auto displacement = this->load<std::int32_t>(
				instruction + displacement_offset );
			const auto absolute = instruction + instruction_size + displacement;
			if constexpr ( std::is_pointer_v<value_t> )
				return reinterpret_cast<value_t>( absolute );
			else
				return static_cast<value_t>( absolute );
		}

		template <typename value_t = std::uintptr_t>
		[[nodiscard]] value_t decode_call( std::uintptr_t instruction ) const noexcept
		{
			return this->decode_rip<value_t>( instruction, 1, 5 );
		}

		[[nodiscard]] std::string load_text( std::uintptr_t address,
			std::size_t capacity = 256 ) const;
		[[nodiscard]] std::uintptr_t module_base( std::string_view module ) const;
		[[nodiscard]] std::uintptr_t scan_signature( std::uintptr_t module,
			std::string_view signature ) const;
		[[nodiscard]] std::uintptr_t scan_code_signature( std::uintptr_t module,
			std::string_view signature ) const;
		[[nodiscard]] std::size_t module_image_size( std::uintptr_t module ) const;
		[[nodiscard]] std::uintptr_t locate_vtable( std::uintptr_t module,
			std::string_view class_name ) const;
		[[nodiscard]] std::uintptr_t locate_vtable_object( std::uintptr_t module,
			std::string_view class_name ) const;

		[[nodiscard]] void* native_handle( ) const noexcept { return m_process; }
		[[nodiscard]] std::uint32_t process_id( ) const noexcept { return m_process_id; }
		[[nodiscard]] explicit operator bool( ) const noexcept { return m_process != nullptr; }

	private:
		struct module_record
		{
			std::uintptr_t base{};
			std::size_t size{};
		};

		[[nodiscard]] module_record find_module( std::string_view name ) const;
		[[nodiscard]] std::uintptr_t find_pointer( std::uintptr_t module,
			std::uintptr_t value, std::uint32_t required_characteristics ) const;

		std::uint32_t m_process_id{};
		void* m_process{};
	};

}
