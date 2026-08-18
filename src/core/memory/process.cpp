#include <stdafx.hpp>

namespace platform::windows {

	namespace {

		class snapshot_handle
		{
		public:
			explicit snapshot_handle( HANDLE value ) noexcept : m_value{ value } {}
			~snapshot_handle( )
			{
				if ( m_value != INVALID_HANDLE_VALUE )
					::CloseHandle( m_value );
			}
			[[nodiscard]] HANDLE get( ) const noexcept { return m_value; }
			[[nodiscard]] explicit operator bool( ) const noexcept
			{
				return m_value != INVALID_HANDLE_VALUE;
			}

		private:
			HANDLE m_value{ INVALID_HANDLE_VALUE };
		};

		struct image_section
		{
			std::uintptr_t begin{};
			std::size_t length{};
			std::uint32_t attributes{};
			std::array<char, 9> name{};
		};

		[[nodiscard]] std::wstring widen( std::string_view text )
		{
			if ( text.empty( ) )
				return {};

			const auto required = ::MultiByteToWideChar(
				CP_UTF8, 0, text.data( ), static_cast<int>( text.size( ) ),
				nullptr, 0 );
			if ( required <= 0 )
				return {};

			std::wstring result( static_cast<std::size_t>( required ), L'\0' );
			::MultiByteToWideChar(
				CP_UTF8, 0, text.data( ), static_cast<int>( text.size( ) ),
				result.data( ), required );
			return result;
		}

		[[nodiscard]] bool readable_page( const MEMORY_BASIC_INFORMATION& page )
		{
			if ( page.State != MEM_COMMIT ||
				( page.Protect & ( PAGE_GUARD | PAGE_NOACCESS ) ) )
				return false;

			const auto protection = page.Protect & 0xffu;
			return protection == PAGE_READONLY ||
				protection == PAGE_READWRITE ||
				protection == PAGE_WRITECOPY ||
				protection == PAGE_EXECUTE_READ ||
				protection == PAGE_EXECUTE_READWRITE ||
				protection == PAGE_EXECUTE_WRITECOPY;
		}

		[[nodiscard]] std::vector<image_section> image_sections(
			const process_session& process, std::uintptr_t module )
		{
			std::vector<image_section> result;
			const auto dos = process.load<IMAGE_DOS_HEADER>( module );
			if ( dos.e_magic != IMAGE_DOS_SIGNATURE || dos.e_lfanew <= 0 )
				return result;

			const auto nt_address = module +
				static_cast<std::uintptr_t>( dos.e_lfanew );
			const auto nt = process.load<IMAGE_NT_HEADERS64>( nt_address );
			if ( nt.Signature != IMAGE_NT_SIGNATURE ||
				nt.FileHeader.NumberOfSections == 0 ||
				nt.FileHeader.NumberOfSections > 96 )
				return result;

			const auto table = nt_address + offsetof(
				IMAGE_NT_HEADERS64, OptionalHeader ) +
				nt.FileHeader.SizeOfOptionalHeader;
			result.reserve( nt.FileHeader.NumberOfSections );
			for ( std::uint16_t index = 0;
				index < nt.FileHeader.NumberOfSections; ++index )
			{
				const auto header = process.load<IMAGE_SECTION_HEADER>(
					table + static_cast<std::uintptr_t>( index ) *
						sizeof( IMAGE_SECTION_HEADER ) );
				image_section section{};
				section.begin = module + header.VirtualAddress;
				section.length = std::max<std::size_t>(
					header.Misc.VirtualSize, header.SizeOfRawData );
				section.attributes = header.Characteristics;
				std::memcpy( section.name.data( ), header.Name, 8 );
				result.push_back( section );
			}
			return result;
		}

		struct signature_byte
		{
			std::uint8_t value{};
			bool wildcard{};
		};

		[[nodiscard]] std::vector<signature_byte> decode_signature(
			std::string_view text )
		{
			const auto nibble = [ ]( char character ) -> int
			{
				if ( character >= '0' && character <= '9' )
					return character - '0';
				if ( character >= 'A' && character <= 'F' )
					return character - 'A' + 10;
				if ( character >= 'a' && character <= 'f' )
					return character - 'a' + 10;
				return -1;
			};

			std::vector<signature_byte> result;
			for ( std::size_t cursor = 0; cursor < text.size( ); )
			{
				while ( cursor < text.size( ) &&
					std::isspace( static_cast<unsigned char>( text[ cursor ] ) ) )
					++cursor;
				if ( cursor == text.size( ) )
					break;
				if ( text[ cursor ] == '?' )
				{
					result.push_back( { 0, true } );
					while ( cursor < text.size( ) && text[ cursor ] == '?' )
						++cursor;
					continue;
				}

				const auto high = nibble( text[ cursor++ ] );
				if ( high < 0 )
					continue;
				const auto low = cursor < text.size( ) ?
					nibble( text[ cursor ] ) : -1;
				if ( low >= 0 )
				{
					++cursor;
					result.push_back( {
						static_cast<std::uint8_t>( high * 16 + low ), false } );
				}
			}
			return result;
		}

		template <typename matcher_t>
		[[nodiscard]] std::uintptr_t scan_readable(
			const process_session& process, std::uintptr_t begin,
			std::size_t length, std::size_t overlap, matcher_t&& matcher )
		{
			constexpr std::size_t block_size{ 256 * 1024 };
			const auto finish = begin + length;
			auto cursor = begin;
			while ( cursor < finish )
			{
				MEMORY_BASIC_INFORMATION page{};
				if ( !::VirtualQueryEx( process.native_handle( ),
					reinterpret_cast<const void*>( cursor ), &page, sizeof( page ) ) )
					break;

				const auto page_begin = std::max(
					cursor, reinterpret_cast<std::uintptr_t>( page.BaseAddress ) );
				const auto page_finish = std::min(
					finish, reinterpret_cast<std::uintptr_t>( page.BaseAddress ) +
						page.RegionSize );
				if ( readable_page( page ) && page_finish > page_begin )
				{
					for ( auto block = page_begin; block < page_finish;
						block += block_size )
					{
						const auto bytes = std::min<std::size_t>(
							block_size + overlap, page_finish - block );
						std::vector<std::byte> buffer( bytes );
						if ( !process.copy( block, buffer.data( ), buffer.size( ) ) )
							continue;
						const auto offset = matcher( buffer );
						if ( offset != std::numeric_limits<std::size_t>::max( ) )
							return block + offset;
					}
				}
				cursor = page_finish > cursor ? page_finish : cursor + 0x1000;
			}
			return 0;
		}

		[[nodiscard]] std::uintptr_t scan_exact(
			const process_session& process, std::uintptr_t begin,
			std::size_t length, std::span<const std::byte> needle )
		{
			if ( needle.empty( ) || needle.size( ) > length )
				return 0;
			return scan_readable( process, begin, length, needle.size( ) - 1,
				[ needle ]( const std::vector<std::byte>& bytes )
				{
					const auto found = std::search(
						bytes.begin( ), bytes.end( ),
						needle.begin( ), needle.end( ) );
					return found == bytes.end( ) ?
						std::numeric_limits<std::size_t>::max( ) :
						static_cast<std::size_t>(
							std::distance( bytes.begin( ), found ) );
				} );
		}

	}

	process_session::~process_session( )
	{
		detach( );
	}

	bool process_session::attach( std::wstring_view executable )
	{
		detach( );
		snapshot_handle snapshot{
			::CreateToolhelp32Snapshot( TH32CS_SNAPPROCESS, 0 ) };
		if ( !snapshot )
			return false;

		PROCESSENTRY32W process{ .dwSize = sizeof( process ) };
		if ( ::Process32FirstW( snapshot.get( ), &process ) )
		{
			do
			{
				if ( ::CompareStringOrdinal(
					process.szExeFile, -1, executable.data( ),
					static_cast<int>( executable.size( ) ), TRUE ) == CSTR_EQUAL )
				{
					m_process_id = process.th32ProcessID;
					break;
				}
			}
			while ( ::Process32NextW( snapshot.get( ), &process ) );
		}
		if ( !m_process_id )
			return false;

		m_process = ::OpenProcess(
			PROCESS_QUERY_INFORMATION | PROCESS_VM_READ | SYNCHRONIZE,
			FALSE, m_process_id );
		if ( !m_process )
		{
			m_process_id = 0;
			return false;
		}
		return true;
	}

	void process_session::detach( ) noexcept
	{
		if ( m_process )
			::CloseHandle( m_process );
		m_process = nullptr;
		m_process_id = 0;
	}

	bool process_session::copy( std::uintptr_t address, void* destination,
		std::size_t bytes ) const noexcept
	{
		if ( !m_process || !address || !destination || !bytes )
			return false;

		const auto started = platform::performance::timestamp( );
		SIZE_T transferred{};
		const auto succeeded = ::ReadProcessMemory(
			m_process, reinterpret_cast<const void*>( address ),
			destination, bytes, &transferred ) != FALSE &&
			transferred == bytes;
		platform::performance::record_rpm( bytes, succeeded, started );
		return succeeded;
	}

	process_session::module_record process_session::find_module(
		std::string_view name ) const
	{
		if ( !m_process_id )
			return {};

		const auto expected = widen( name );
		if ( expected.empty( ) )
			return {};

		snapshot_handle snapshot{ ::CreateToolhelp32Snapshot(
			TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, m_process_id ) };
		if ( !snapshot )
			return {};

		MODULEENTRY32W module{ .dwSize = sizeof( module ) };
		if ( ::Module32FirstW( snapshot.get( ), &module ) )
		{
			do
			{
				if ( ::CompareStringOrdinal(
					module.szModule, -1, expected.c_str( ), -1, TRUE ) ==
					CSTR_EQUAL )
				{
					return {
						reinterpret_cast<std::uintptr_t>( module.modBaseAddr ),
						module.modBaseSize };
				}
			}
			while ( ::Module32NextW( snapshot.get( ), &module ) );
		}
		return {};
	}

	std::uintptr_t process_session::module_base( std::string_view module ) const
	{
		return find_module( module ).base;
	}

	std::string process_session::load_text( std::uintptr_t address,
		std::size_t capacity ) const
	{
		if ( !address || !capacity )
			return {};
		const auto bounded = std::min<std::size_t>( capacity, 4096 );
		std::array<char, 256> local{};
		if ( bounded <= local.size( ) )
		{
			if ( !copy( address, local.data( ), bounded ) )
				return {};
			const auto terminator = std::find(
				local.begin( ), local.begin( ) + bounded, '\0' );
			return { local.begin( ), terminator };
		}

		std::vector<char> buffer( bounded );
		if ( !copy( address, buffer.data( ), buffer.size( ) ) )
			return {};
		const auto terminator = std::find(
			buffer.begin( ), buffer.end( ), '\0' );
		return { buffer.begin( ), terminator };
	}

	std::uintptr_t process_session::scan_signature( std::uintptr_t module,
		std::string_view signature ) const
	{
		const auto pattern = decode_signature( signature );
		if ( !module || pattern.empty( ) )
			return 0;

		const auto dos = load<IMAGE_DOS_HEADER>( module );
		const auto nt = dos.e_magic == IMAGE_DOS_SIGNATURE ?
			load<IMAGE_NT_HEADERS64>(
				module + static_cast<std::uintptr_t>( dos.e_lfanew ) ) :
			IMAGE_NT_HEADERS64{};
		if ( nt.Signature != IMAGE_NT_SIGNATURE )
			return 0;

		return scan_readable( *this, module,
			nt.OptionalHeader.SizeOfImage, pattern.size( ) - 1,
			[ &pattern ]( const std::vector<std::byte>& bytes )
			{
				for ( std::size_t offset = 0;
					offset + pattern.size( ) <= bytes.size( ); ++offset )
				{
					auto matched = true;
					for ( std::size_t index = 0;
						index < pattern.size( ); ++index )
					{
						if ( !pattern[ index ].wildcard &&
							std::to_integer<std::uint8_t>( bytes[ offset + index ] ) !=
								pattern[ index ].value )
						{
							matched = false;
							break;
						}
					}
					if ( matched )
						return offset;
				}
				return std::numeric_limits<std::size_t>::max( );
			} );
	}

	std::uintptr_t process_session::scan_code_signature(
		std::uintptr_t module, std::string_view signature ) const
	{
		const auto pattern = decode_signature( signature );
		if ( !module || pattern.empty( ) )
			return 0;

		for ( const auto& section : image_sections( *this, module ) )
		{
			if ( !( section.attributes & IMAGE_SCN_CNT_CODE )
				|| !( section.attributes & IMAGE_SCN_MEM_READ ) )
				continue;

			const auto match = scan_readable( *this, section.begin,
				section.length, pattern.size( ) - 1,
				[ &pattern ]( const std::vector<std::byte>& bytes )
				{
					for ( std::size_t offset{};
						offset + pattern.size( ) <= bytes.size( ); ++offset )
					{
						auto matched = true;
						for ( std::size_t index{}; index < pattern.size( ); ++index )
						{
							if ( !pattern[ index ].wildcard
								&& std::to_integer<std::uint8_t>(
									bytes[ offset + index ] ) != pattern[ index ].value )
							{
								matched = false;
								break;
							}
						}
						if ( matched ) return offset;
					}
					return std::numeric_limits<std::size_t>::max( );
				} );
			if ( match ) return match;
		}
		return 0;
	}

	std::size_t process_session::module_image_size(
		std::uintptr_t module ) const
	{
		if ( !module ) return 0;
		const auto dos = load<IMAGE_DOS_HEADER>( module );
		if ( dos.e_magic != IMAGE_DOS_SIGNATURE || dos.e_lfanew <= 0 )
			return 0;
		const auto nt = load<IMAGE_NT_HEADERS64>(
			module + static_cast<std::uintptr_t>( dos.e_lfanew ) );
		return nt.Signature == IMAGE_NT_SIGNATURE
			? static_cast<std::size_t>( nt.OptionalHeader.SizeOfImage ) : 0;
	}

	std::uintptr_t process_session::find_pointer( std::uintptr_t module,
		std::uintptr_t value, std::uint32_t required_characteristics ) const
	{
		const auto needle = std::as_bytes( std::span{ &value, 1 } );
		for ( const auto& section : image_sections( *this, module ) )
		{
			if ( ( section.attributes & required_characteristics ) !=
				required_characteristics )
				continue;
			if ( const auto found = scan_exact(
				*this, section.begin, section.length, needle ) )
				return found;
		}
		return 0;
	}

	std::uintptr_t process_session::locate_vtable( std::uintptr_t module,
		std::string_view class_name ) const
	{
		const auto decorated = ".?AV" + std::string{ class_name } + "@@";
		const auto bytes = std::as_bytes( std::span{
			decorated.data( ), decorated.size( ) + 1 } );

		std::uintptr_t type_descriptor{};
		for ( const auto& section : image_sections( *this, module ) )
		{
			if ( !( section.attributes & IMAGE_SCN_CNT_INITIALIZED_DATA ) ||
				!( section.attributes & IMAGE_SCN_MEM_READ ) )
				continue;
			if ( const auto name = scan_exact(
				*this, section.begin, section.length, bytes ) )
			{
				type_descriptor = name - 0x10;
				break;
			}
		}
		if ( !type_descriptor )
			return 0;

		const auto descriptor_rva = static_cast<std::uint32_t>(
			type_descriptor - module );
		std::uintptr_t complete_locator{};
		for ( const auto& section : image_sections( *this, module ) )
		{
			if ( std::string_view{ section.name.data( ) } != ".rdata" )
				continue;
			complete_locator = scan_readable(
				*this, section.begin, section.length, 0x20,
				[ descriptor_rva ]( const std::vector<std::byte>& data )
				{
					for ( std::size_t offset = 0;
						offset + 0x18 <= data.size( ); offset += alignof( void* ) )
					{
						std::uint32_t candidate{};
						std::memcpy( &candidate, data.data( ) + offset + 0x0c,
							sizeof( candidate ) );
						if ( candidate == descriptor_rva )
							return offset;
					}
					return std::numeric_limits<std::size_t>::max( );
				} );
			if ( complete_locator )
				break;
		}
		if ( !complete_locator )
			return 0;

		const auto reference = find_pointer(
			module, complete_locator, IMAGE_SCN_MEM_READ );
		return reference ? reference + sizeof( std::uintptr_t ) : 0;
	}

	std::uintptr_t process_session::locate_vtable_object(
		std::uintptr_t module, std::string_view class_name ) const
	{
		const auto table = locate_vtable( module, class_name );
		return table ? find_pointer(
			module, table, IMAGE_SCN_MEM_READ | IMAGE_SCN_MEM_WRITE ) : 0;
	}

}
