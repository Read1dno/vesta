#include <stdafx.hpp>

namespace game {
	namespace {
		constexpr std::uintptr_t k_schema_type_scopes{ 0x190 };
		constexpr std::uintptr_t k_schema_registration_count{ 0x280 };
		constexpr std::uintptr_t k_scope_name{ 0x8 };
		constexpr std::uintptr_t k_scope_class_bindings{ 0x560 };
		constexpr std::uintptr_t k_hash_pool_allocated{ 0x0c };
		constexpr std::uintptr_t k_hash_pool_peak{ 0x10 };
		constexpr std::uintptr_t k_hash_free_head{ 0x20 };
		constexpr std::uintptr_t k_hash_buckets{ 0x60 };
		constexpr std::size_t k_hash_bucket_count{ 256 };
		constexpr std::size_t k_hash_bucket_size{ 0x18 };
		constexpr std::uintptr_t k_bucket_first{ 0x8 };
		constexpr std::uintptr_t k_bucket_first_uncommitted{ 0x10 };
		constexpr std::uintptr_t k_hash_fixed_node_next{ 0x8 };
		constexpr std::uintptr_t k_hash_blob_node_next{ 0x0 };
		constexpr std::uintptr_t k_hash_node_data{ 0x10 };

		struct utl_vector_header
		{
			std::int32_t count{};
			std::int32_t reserved{};
			std::uintptr_t data{};
		};
		static_assert( sizeof( utl_vector_header ) == 0x10 );

		struct schema_field_wire
		{
			std::uintptr_t name{};
			std::uintptr_t type{};
			std::int32_t offset{};
			std::int32_t metadata_count{};
			std::uintptr_t metadata{};
		};
		static_assert( sizeof( schema_field_wire ) == 0x20 );

		[[nodiscard]] constexpr std::uint64_t make_key(
			const std::uint32_t class_id, const std::uint32_t field_id ) noexcept
		{
			return static_cast<std::uint64_t>( class_id ) << 32 | field_id;
		}

		[[nodiscard]] bool plausible_pointer( const std::uintptr_t value ) noexcept
		{
			return value >= 0x10000ull && value < 0x0000800000000000ull;
		}

		struct hash_walk_stats
		{
			std::size_t peak{};
			std::size_t nodes{};
			std::size_t data{};
			bool complete{};
		};

		[[nodiscard]] hash_walk_stats enumerate_hash_data(
			const platform::windows::process_session& process,
			const std::uintptr_t hash, std::vector<std::uintptr_t>& result )
		{
			if ( !plausible_pointer( hash ) ) return {};
			const auto allocated = process.load<std::int32_t>(
				hash + k_hash_pool_allocated );
			const auto peak = process.load<std::int32_t>( hash + k_hash_pool_peak );
			const auto expected = static_cast<std::size_t>( std::clamp(
				std::max( allocated, peak ), 0, 32768 ) );
			const auto node_limit = std::clamp<std::size_t>(
				expected * 2 + 512, 1024, 65536 );

			std::unordered_set<std::uintptr_t> visited_nodes;
			std::unordered_set<std::uintptr_t> seen_data;
			visited_nodes.reserve( node_limit );
			seen_data.reserve( std::max<std::size_t>( expected, 1024 ) );
			const auto append_chain = [&]( std::uintptr_t node,
				const std::uintptr_t next_offset )
			{
				while ( plausible_pointer( node )
					&& visited_nodes.size( ) < node_limit
					&& visited_nodes.insert( node ).second )
				{
					const auto data = process.load<std::uintptr_t>(
						node + k_hash_node_data );
					if ( plausible_pointer( data ) && seen_data.insert( data ).second )
						result.push_back( data );
					const auto next = process.load<std::uintptr_t>(
						node + next_offset );
					if ( next == node ) break;
					node = next;
				}
			};

			for ( std::size_t bucket{}; bucket < k_hash_bucket_count; ++bucket )
			{
				const auto address = hash + k_hash_buckets
					+ bucket * k_hash_bucket_size;
				append_chain( process.load<std::uintptr_t>(
					address + k_bucket_first_uncommitted ),
					k_hash_fixed_node_next );
				append_chain( process.load<std::uintptr_t>(
					address + k_bucket_first ), k_hash_fixed_node_next );
			}

			append_chain( process.load<std::uintptr_t>( hash + k_hash_free_head ),
				k_hash_blob_node_next );

			return {
				static_cast<std::size_t>( std::max( peak, 0 ) ),
				visited_nodes.size( ), seen_data.size( ),
				allocated > 0 && peak >= allocated && peak <= 32768
					&& seen_data.size( ) == static_cast<std::size_t>( peak )
			};
		}

		[[nodiscard]] bool is_client_scope( std::string name )
		{
			std::ranges::transform( name, name.begin( ), []( const unsigned char c )
			{
				return static_cast<char>( std::tolower( c ) );
			} );
			return name == "client.dll" || name == "client";
		}

	}

	bool field_catalog::initialize( )
	{
		this->m_entries.clear( );
		const auto& process = app::context().process;
		const auto module = app::context().modules.schema;
		const auto image_size = process.module_image_size( module );
		const auto match = process.scan_code_signature(
			module, "4C 8D 35 ? ? ? ? 0F 28 45" );
		const auto schema_system = match ? process.decode_rip( match ) : 0;
		if ( !schema_system || !image_size || schema_system < module
			|| schema_system + k_schema_registration_count + sizeof( std::int32_t )
				> module + image_size )
		{
			app::context().diagnostics.warning(
				"[schemas] live SchemaSystem signature unavailable; startup rejected." );
			return false;
		}

		const auto registration_count = process.load<std::int32_t>(
			schema_system + k_schema_registration_count );
		const auto scopes = process.load<utl_vector_header>(
			schema_system + k_schema_type_scopes );
		if ( registration_count <= 0 || scopes.count <= 0 || scopes.count > 256
			|| !plausible_pointer( scopes.data ) )
		{
			app::context().diagnostics.warning(
				"[schemas] invalid live registry (registrations={}, scopes={}); startup rejected.",
				registration_count, scopes.count );
			return false;
		}

		std::vector<std::uintptr_t> scope_pointers(
			static_cast<std::size_t>( scopes.count ) );
		if ( !process.copy( scopes.data, scope_pointers.data( ),
			scope_pointers.size( ) * sizeof( std::uintptr_t ) ) )
		{
			app::context().diagnostics.warning(
				"[schemas] failed to read live type scopes; startup rejected." );
			return false;
		}

		std::vector<field_entry> discovered;
		discovered.reserve( 4096 );
		std::size_t client_classes{};
		hash_walk_stats class_hash{};
		for ( const auto scope : scope_pointers )
		{
			if ( !plausible_pointer( scope )
				|| !is_client_scope( process.load_text( scope + k_scope_name, 256 ) ) )
				continue;

			std::vector<std::uintptr_t> bindings;
			bindings.reserve( 2048 );
			class_hash = enumerate_hash_data(
				process, scope + k_scope_class_bindings, bindings );
			for ( const auto binding : bindings )
			{
				const auto class_name_address = process.load<std::uintptr_t>( binding + 0x8 );
				const auto fields_address = process.load<std::uintptr_t>( binding + 0x30 );
				const auto field_count = process.load<std::int16_t>( binding + 0x24 );
				const auto class_size = process.load<std::int32_t>( binding + 0x20 );
				if ( !plausible_pointer( class_name_address )
					|| !plausible_pointer( fields_address ) || field_count <= 0
					|| field_count > 4096 || class_size <= 0 || class_size > 0x100000 )
					continue;

				const auto class_name = process.load_text( class_name_address, 192 );
				if ( class_name.empty( ) ) continue;
				std::vector<schema_field_wire> fields(
					static_cast<std::size_t>( field_count ) );
				if ( !process.copy( fields_address, fields.data( ),
					fields.size( ) * sizeof( schema_field_wire ) ) )
					continue;

				++client_classes;
				const auto class_id = identity::of( class_name );
				for ( const auto& field : fields )
				{
					if ( !plausible_pointer( field.name )
						|| !plausible_pointer( field.type ) || field.offset < 0
						|| field.offset >= class_size )
						continue;
					const auto field_name = process.load_text( field.name, 192 );
					if ( field_name.empty( ) ) continue;
					discovered.push_back( {
						make_key( class_id, identity::of( field_name ) ), field.offset } );
				}
			}
			break;
		}

		std::sort( discovered.begin( ), discovered.end( ) );
		std::vector<field_entry> unique;
		unique.reserve( discovered.size( ) );
		std::size_t ambiguous{};
		for ( std::size_t begin{}; begin < discovered.size( ); )
		{
			auto end = begin + 1;
			while ( end < discovered.size( )
				&& discovered[ end ].key == discovered[ begin ].key ) ++end;
			const auto conflict = std::any_of( discovered.begin( ) + begin + 1,
				discovered.begin( ) + end, [&]( const field_entry& entry )
				{
					return entry.offset != discovered[ begin ].offset;
				} );
			if ( conflict ) ++ambiguous;
			else unique.push_back( discovered[ begin ] );
			begin = end;
		}

		const auto live_has = [&]( const std::string_view class_name,
			const std::string_view field_name )
		{
			const auto key = make_key(
				identity::of( class_name ), identity::of( field_name ) );
			const auto found = std::lower_bound( unique.begin( ), unique.end( ),
				field_entry{ key, 0 } );
			return found != unique.end( ) && found->key == key && found->offset > 0;
		};
		const auto complete = class_hash.complete && client_classes >= 300
			&& unique.size( ) >= 3000 && ambiguous == 0
			&& live_has( "C_BaseEntity", "m_iHealth" )
			&& live_has( "C_BaseEntity", "m_iTeamNum" )
			&& live_has( "C_BaseEntity", "m_pGameSceneNode" )
			&& live_has( "CCSPlayerController", "m_hPlayerPawn" )
			&& live_has( "CGameSceneNode", "m_vecAbsOrigin" );
		if ( !complete )
		{
			app::context().diagnostics.warning(
				"[schemas] rejected partial live dump (classes={}, fields={}, ambiguous={}, hash={}/{}); startup rejected.",
				client_classes, unique.size( ), ambiguous, class_hash.data,
				class_hash.peak );
			return false;
		}

		this->m_entries = std::move( unique );
		app::context().diagnostics.success(
			"[schemas] live client.dll catalog ready: {} classes, {} fields, complete hash walk {}/{}.",
			client_classes, this->m_entries.size( ), class_hash.data,
			class_hash.peak );
		return true;
	}

	std::int32_t field_catalog::lookup( const char* class_name,
		std::uint32_t field_id )
	{
		if ( !class_name || !*class_name ) return 0;
		const auto class_id = identity::of( class_name );
		const auto key = make_key( class_id, field_id );
		const auto found = std::lower_bound( this->m_entries.begin( ),
			this->m_entries.end( ), field_entry{ key, 0 } );
		if ( found != this->m_entries.end( ) && found->key == key )
			return found->offset;
		static std::mutex warning_mutex{};
		static std::unordered_set<std::uint64_t> warned{};
		{
			std::scoped_lock lock( warning_mutex );
			if ( warned.insert( key ).second )
				app::context().diagnostics.warning(
					"[schemas] required live field missing for {}:{:#x}; lookup rejected.",
					class_name, field_id );
		}
		return 0;
	}

}
