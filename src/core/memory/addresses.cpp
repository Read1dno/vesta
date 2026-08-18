#include <stdafx.hpp>

bool game::address_catalog::initialize( )
{
	const auto& process = app::context().process;
	const auto client = app::context().modules.client;
	const auto image_size = process.module_image_size( client );
	if ( !client || !image_size )
		return false;

	const auto resolve = [&]( const std::string_view name,
		const std::string_view pattern ) -> std::uintptr_t
	{
		const auto match = process.scan_code_signature( client, pattern );
		const auto target = match ? process.decode_rip( match ) : 0;
		if ( !target || target < client || target >= client + image_size )
		{
			app::context().diagnostics.warning(
				"[offsets] {} pattern is outdated or resolved outside client.dll.", name );
			return 0;
		}
		std::byte probe{};
		if ( !process.copy( target, &probe, sizeof( probe ) ) )
		{
			app::context().diagnostics.warning(
				"[offsets] {} resolved to unreadable memory at client.dll+{:#x}.",
				name, target - client );
			return 0;
		}
		return target;
	};

	csgo_input = resolve( "dwCSGOInput",
		"48 89 05 ? ? ? ? 0F 57 C0 0F 11 05" );
	entity_list = resolve( "dwEntityList",
		"48 89 0D ? ? ? ? E9 ? ? ? ? CC" );
	local_player_controller = resolve( "dwLocalPlayerController",
		"48 8B 05 ? ? ? ? 41 89 BE" );
	global_vars = resolve( "dwGlobalVars",
		"48 89 15 ? ? ? ? 48 89 42" );
	view_matrix = resolve( "dwViewMatrix",
		"48 8D 0D ? ? ? ? 48 C1 E0 06" );

	if ( const auto match = process.scan_code_signature( client,
		"48 89 05 ? ? ? ? E8 ? ? ? ? 48 85 DB" ) )
	{
		const auto target = process.decode_rip( match );
		if ( target >= client && target < client + image_size )
			auto_accept = target;
	}

	const auto complete = entity_list && local_player_controller
		&& global_vars && view_matrix;
	if ( complete )
	{
		app::context().diagnostics.success(
			"[offsets] live client.dll globals resolved from the current image." );
	}
	return complete;
}
