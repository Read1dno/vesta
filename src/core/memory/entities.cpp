#include <stdafx.hpp>

namespace game {

	void entity_directory::refresh( )
	{
		const auto entity_list = this->get_entity_list( );
		if ( !entity_list )
		{

			return;
		}

		std::uintptr_t chunk_ptrs[ 4 ]{};
		if ( !app::context().process.copy( entity_list + 0x10, chunk_ptrs, sizeof( chunk_ptrs ) ) )
		{

			return;
		}

		std::vector<cached> fresh{};
		fresh.reserve( 64 );

		thread_local std::unordered_set<std::uintptr_t> seen{};
		seen.clear( );
		if ( seen.bucket_count( ) < 256 )
		{
			seen.reserve( 256 );
		}

		constexpr std::size_t k_entries_per_chunk{ 512 };
		constexpr std::size_t k_entry_stride{ 0x70 };
		constexpr std::size_t k_chunk_size{ k_entries_per_chunk * k_entry_stride };

		thread_local std::array<std::vector<std::uint8_t>, 4> chunk_buffers{};
		for ( auto& buffer : chunk_buffers )
		{
			if ( buffer.size( ) != k_chunk_size ) buffer.resize( k_chunk_size );
		}

		for ( std::int32_t ci = 0; ci < 4; ++ci )
		{
			if ( chunk_ptrs[ ci ] && !app::context().process.copy(
				chunk_ptrs[ ci ], chunk_buffers[ ci ].data( ), k_chunk_size ) )
			{
				return;
			}
		}

		auto& cache = this->m_class_cache;
		static std::uint32_t validation_phase{};
		validation_phase = ( validation_phase + 1 ) & 7u;

		for ( std::int32_t ci = 0; ci < 4; ++ci )
		{
			const auto chunk_addr = chunk_ptrs[ ci ];
			if ( !chunk_addr )
			{
				continue;
			}

			const auto& chunk_buffer = chunk_buffers[ ci ];

			for ( std::size_t ei = 0; ei < k_entries_per_chunk; ++ei )
			{
				std::uintptr_t entity{};
				std::memcpy( &entity, chunk_buffer.data( ) + ei * k_entry_stride, sizeof( entity ) );

				if ( !entity || entity < 0x10000 )
				{
					continue;
				}
				seen.insert( entity );

				const auto index = static_cast<std::int32_t>( ci * k_entries_per_chunk + ei );

				if ( auto it = cache.find( entity ); it != cache.end( ) )
				{

					const auto audit_identity =
						( ( static_cast<std::uint32_t>( index ) + validation_phase ) & 7u ) == 0u;
					if ( !audit_identity )
					{
						if ( it->second.entity_type != type::unknown )
						{
							fresh.push_back( { .ptr = entity, .schema_id = it->second.schema_id,
								.index = index, .type = it->second.entity_type } );
						}
						continue;
					}

					const auto identity_8 = app::context().process.load<std::uintptr_t>( entity + 0x8 );
					const auto identity_10 = app::context().process.load<std::uintptr_t>( entity + 0x10 );
					const auto identity_matches = it->second.entity_identity != 0 &&
						( identity_8 == it->second.entity_identity || identity_10 == it->second.entity_identity );
					const auto class_info = identity_matches
						? app::context().process.load<std::uintptr_t>( it->second.entity_identity + 0x8 )
						: 0;

					if ( identity_matches && class_info == it->second.entity_class_info )
					{
						if ( it->second.entity_type == type::unknown )
						{
							continue;
						}

						fresh.push_back( { .ptr = entity, .schema_id = it->second.schema_id, .index = index, .type = it->second.entity_type } );
						continue;
					}

					cache.erase( it );
				}

				std::uintptr_t identity{};
				std::uintptr_t class_info{};
				const auto schema_id = this->get_schema_id( entity, identity, class_info );
				if ( !schema_id )
				{

					continue;
				}

				const auto entity_type = this->classify( schema_id );

				cache.emplace( entity, class_cache_entry{ identity, class_info, schema_id, entity_type } );

				if ( entity_type == type::unknown )
				{
					continue;
				}

				fresh.push_back( { .ptr = entity, .schema_id = schema_id, .index = index, .type = entity_type } );
			}
		}

		for ( auto it = cache.begin( ); it != cache.end( ); )
		{
			if ( !seen.contains( it->first ) )
				it = cache.erase( it );
			else
				++it;
		}

		this->m_entities.store(
			std::make_shared<const std::vector<cached>>( std::move( fresh ) ),
			std::memory_order_release );
	}

	std::uintptr_t entity_directory::lookup( std::uint32_t handle ) const
	{
		return this->lookup_slot( handle, true );
	}

	std::uintptr_t entity_directory::lookup_index( std::uint32_t index ) const
	{
		if ( index == 0 || index > 0x7fff )
		{
			return 0;
		}

		return this->lookup_slot( index, false );
	}

	std::uintptr_t entity_directory::lookup_slot(
		std::uint32_t value, bool validate_serial ) const
	{
		if ( !value || value == 0xffffffff )
		{
			return 0;
		}

		const auto entity_list = this->get_entity_list( );
		if ( !entity_list )
		{
			return 0;
		}

		const auto list_entry = app::context().process.load<std::uintptr_t>( entity_list + ( static_cast< std::uintptr_t >( ( value & 0x7fff ) >> 9 ) * 8 ) + 0x10 );
		if ( !list_entry )
		{
			return 0;
		}

		const auto identity = list_entry
			+ ( static_cast<std::uintptr_t>( value & 0x1ff ) * 112 );
		const auto entity = app::context().process.load<std::uintptr_t>( identity );
		if ( !entity || entity < 0x10000 )
		{
			return 0;
		}

		if ( validate_serial )
		{
			const auto resident_handle = app::context().process.load<std::uint32_t>(
				identity + 0x10 );
			if ( resident_handle != 0 && resident_handle != 0xffffffff
				&& resident_handle != value )
			{
				return 0;
			}
		}

		return entity;
	}

	std::vector<entity_directory::cached> entity_directory::by_type( type filter ) const
	{
		const auto snapshot = this->all( );
		std::vector<cached> result{};
		result.reserve( snapshot->size( ) );

		for ( const auto& entry : *snapshot )
		{
			if ( entry.type == filter )
			{
				result.push_back( entry );
			}
		}

		return result;
	}

	std::shared_ptr<const std::vector<entity_directory::cached>>
		entity_directory::all( ) const
	{
		return this->m_entities.load( std::memory_order_acquire );
	}

	std::uintptr_t entity_directory::get_entity_list( ) const
	{
		return app::context().process.load<std::uintptr_t>( app::context().addresses.entity_list );
	}

	namespace {

		bool is_valid_class_name( const char* name )
		{
			if ( name[ 0 ] != 'C' )
			{
				return false;
			}

			std::size_t len{ 0 };
			for ( ; name[ len ]; ++len )
			{
				if ( len >= 63 )
				{
					return false;
				}

				const auto c = name[ len ];
				const auto ok = ( c >= 'A' && c <= 'Z' ) || ( c >= 'a' && c <= 'z' ) || ( c >= '0' && c <= '9' ) || c == '_';
				if ( !ok )
				{
					return false;
				}
			}

			return len >= 3;
		}

	}

	std::uint32_t entity_directory::get_schema_id( std::uintptr_t entity, std::uintptr_t& resolved_identity, std::uintptr_t& resolved_class_info ) const
	{
		resolved_identity = 0;
		resolved_class_info = 0;

		struct chain
		{
			std::uint32_t ident_off;
			std::uint32_t name_off;
			bool extra_deref;
		};

		static constexpr chain k_chains[ ]{
			{ 0x10, 0x8, false }, { 0x10, 0x8, true },
			{ 0x10, 0x10, false }, { 0x10, 0x10, true },
			{ 0x8, 0x8, false }, { 0x8, 0x8, true },
			{ 0x8, 0x10, false }, { 0x8, 0x10, true },
		};

		static std::atomic<int> s_active_chain{ -1 };

		const auto try_chain = [ entity ]( const chain& c, char( &name )[ 64 ], std::uintptr_t& identity_out, std::uintptr_t& class_info_out ) -> bool
		{
			const auto identity = app::context().process.load<std::uintptr_t>( entity + c.ident_off );
			if ( identity < 0x10000 )
			{
				return false;
			}

			const auto class_info = app::context().process.load<std::uintptr_t>( identity + 0x8 );
			if ( class_info < 0x10000 )
			{
				return false;
			}

			auto name_ptr = app::context().process.load<std::uintptr_t>( class_info + c.name_off );
			if ( name_ptr < 0x10000 )
			{
				return false;
			}

			if ( c.extra_deref )
			{
				name_ptr = app::context().process.load<std::uintptr_t>( name_ptr + 0x8 );
				if ( name_ptr < 0x10000 )
				{
					return false;
				}
			}

			std::memset( name, 0, sizeof( name ) );
			if ( !app::context().process.copy( name_ptr, name, sizeof( name ) - 1 ) )
			{
				return false;
			}

			if ( !is_valid_class_name( name ) )
			{
				return false;
			}

			identity_out = identity;
			class_info_out = class_info;
			return true;
		};

		char class_name[ 64 ]{};

		const auto cached = s_active_chain.load( std::memory_order_relaxed );
		if ( cached >= 0 && try_chain( k_chains[ cached ], class_name, resolved_identity, resolved_class_info ) )
		{
			return identity::of( class_name );
		}

		for ( int i = 0; i < static_cast< int >( std::size( k_chains ) ); ++i )
		{
			if ( i == cached )
			{
				continue;
			}

			if ( try_chain( k_chains[ i ], class_name, resolved_identity, resolved_class_info ) )
			{
				s_active_chain.store( i, std::memory_order_relaxed );
				app::context().diagnostics.info( "[diag] entity class chain resolved: ident+{:#x} name+{:#x} deref={} (\"{}\")",
					k_chains[ i ].ident_off, k_chains[ i ].name_off, k_chains[ i ].extra_deref, class_name );
				return identity::of( class_name );
			}
		}

		return 0;
	}

	entity_directory::type entity_directory::classify( std::uint32_t schema_id ) const
	{
		switch ( schema_id )
		{
		case "CCSPlayerController"_id:
			return type::player;

		case "C_PlantedC4"_id:
			return type::item;

		case "C_AK47"_id:
		case "C_WeaponM4A1"_id:
		case "C_WeaponM4A1Silencer"_id:
		case "C_WeaponAWP"_id:
		case "C_WeaponAug"_id:
		case "C_WeaponFamas"_id:
		case "C_WeaponGalilAR"_id:
		case "C_WeaponSG556"_id:
		case "C_WeaponG3SG1"_id:
		case "C_WeaponSCAR20"_id:
		case "C_WeaponSSG08"_id:
		case "C_WeaponMAC10"_id:
		case "C_WeaponMP5SD"_id:
		case "C_WeaponMP7"_id:
		case "C_WeaponMP9"_id:
		case "C_WeaponBizon"_id:
		case "C_WeaponP90"_id:
		case "C_WeaponUMP45"_id:
		case "C_WeaponNOVA"_id:
		case "C_WeaponSawedoff"_id:
		case "C_WeaponXM1014"_id:
		case "C_WeaponMag7"_id:
		case "C_WeaponM249"_id:
		case "C_WeaponNegev"_id:
		case "C_DEagle"_id:
		case "C_WeaponElite"_id:
		case "C_WeaponFiveSeven"_id:
		case "C_WeaponGlock"_id:
		case "C_WeaponHKP2000"_id:
		case "C_WeaponUSPSilencer"_id:
		case "C_WeaponP250"_id:
		case "C_WeaponCZ75a"_id:
		case "C_WeaponTec9"_id:
		case "C_WeaponRevolver"_id:
		case "C_WeaponTaser"_id:
		case "C_Knife"_id:
		case "C_C4"_id:
		case "C_Item_Healthshot"_id:
		case "C_HEGrenade"_id:
		case "C_Flashbang"_id:
		case "C_SmokeGrenade"_id:
		case "C_MolotovGrenade"_id:
		case "C_IncendiaryGrenade"_id:
		case "C_DecoyGrenade"_id:
			return type::item;

		case "C_HEGrenadeProjectile"_id:
		case "C_FlashbangProjectile"_id:
		case "C_SmokeGrenadeProjectile"_id:
		case "C_MolotovProjectile"_id:
		case "C_Inferno"_id:
		case "C_DecoyProjectile"_id:
			return type::projectile;

		case "C_BulletHitModel"_id:
		case "C_LocalTempEntity"_id:
			return type::impact;

		default:
			return type::unknown;
		}
	}

}
