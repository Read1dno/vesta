#include <stdafx.hpp>
#include <core/memory/compatibility_contracts.hpp>

namespace game {

	namespace {

		bool is_valid_cvar_name( const char* name )
		{
			if ( !name[ 0 ] )
			{
				return false;
			}

			std::size_t len{ 0 };
			for ( ; name[ len ]; ++len )
			{
				if ( len >= 127 )
				{
					return false;
				}

				const auto c = name[ len ];
				const auto ok = ( c >= 'a' && c <= 'z' ) || ( c >= 'A' && c <= 'Z' ) || ( c >= '0' && c <= '9' ) || c == '_' || c == '.';
				if ( !ok )
				{
					return false;
				}
			}

			return len >= 2;
		}

		// ConVarData current value; +8 points to the default, not the live value.
		std::atomic<std::uint32_t> g_value_offset{ 0x58 };

	} // namespace

	std::uintptr_t variable_registry::find( std::uint32_t name_id )
	{

		static const auto cache = [ ]( ) -> std::unordered_map<std::uint32_t, std::uintptr_t>
		{
			std::unordered_map<std::uint32_t, std::uintptr_t> map{};

			const auto cvar = app::context().process.locate_vtable_object( app::context().modules.tier, "CCvar" );
			if ( !cvar )
			{
				app::context().diagnostics.warning( "[diag] CCvar instance not found -- convars disabled" );
				return map;
			}

			// The list-head slot inside CCvar drifts between builds; probe candidates
			// and keep the one that actually yields a large set of named entries.
			for ( const std::uintptr_t head_off : { 0x48, 0x40, 0x50, 0x58, 0x60, 0x68 } )
			{
				const auto base = app::context().process.load<std::uintptr_t>( cvar + head_off );
				if ( base < 0x10000 )
				{
					continue;
				}

				constexpr std::size_t k_node_stride{ 16 };
				constexpr std::size_t k_nodes_per_chunk{ 4096 };
				constexpr std::size_t k_max_chunks{ 8 }; // 32768 nodes max

				std::vector<std::uint8_t> buffer( k_nodes_per_chunk * k_node_stride );

				for ( std::size_t chunk = 0; chunk < k_max_chunks; ++chunk )
				{
					if ( !app::context().process.copy( base + chunk * buffer.size( ), buffer.data( ), buffer.size( ) ) )
					{
						break;
					}

					for ( std::size_t i = 0; i < k_nodes_per_chunk; ++i )
					{
						std::uintptr_t convar_ptr{};
						std::memcpy( &convar_ptr, buffer.data( ) + i * k_node_stride, sizeof( convar_ptr ) );

						if ( convar_ptr < 0x10000 )
						{
							continue;
						}

						const auto name_ptr = app::context().process.load<std::uintptr_t>( convar_ptr );
						if ( name_ptr < 0x10000 )
						{
							continue;
						}

						char name[ 128 ]{};
						if ( !app::context().process.copy( name_ptr, name, sizeof( name ) - 1 ) )
						{
							continue;
						}

						if ( !is_valid_cvar_name( name ) )
						{
							continue;
						}

						map.try_emplace( identity::of( name ), convar_ptr );
					}
				}

				// A wrong slot yields near-zero plausible names; the real list has thousands.
				if ( map.size( ) >= 200 )
				{
					app::context().diagnostics.info( "[diag] cvar list found at CCvar+{:#x} ({} convars)", head_off, map.size( ) );


					return map;
				}

				map.clear( );
			}

			app::context().diagnostics.warning( "[diag] cvar list not found -- convars disabled" );
			return map;
		}( );

		const auto it = cache.find( name_id );
		return it == cache.end( ) ? 0 : it->second;
	}

	template<typename T>
	T variable_registry::get( std::uintptr_t cvar_ptr )
	{
		if ( !cvar_ptr )
		{
			return T{};
		}

		return app::context().process.load<T>( cvar_ptr + g_value_offset.load( std::memory_order_relaxed ) );
	}

    template<typename T>
    bool variable_registry::try_get(std::uintptr_t ptr, T& value)
    {
        value = {};
        if (!ptr) return false;
        std::array<std::byte, 0x5c> data{};
        if (!app::context().process.copy(ptr, data.data(), data.size())) return false;
        return compatibility::detail::convar_value(data,value);
    }

    template bool variable_registry::try_get<bool>(std::uintptr_t, bool&);
    template bool variable_registry::try_get<int>(std::uintptr_t, int&);
    template bool variable_registry::try_get<float>(std::uintptr_t, float&);

	template int variable_registry::get<int>( std::uintptr_t );
	template float variable_registry::get<float>( std::uintptr_t );
	template bool variable_registry::get<bool>( std::uintptr_t );
	template std::uint8_t variable_registry::get<std::uint8_t>( std::uintptr_t );

} // namespace game
