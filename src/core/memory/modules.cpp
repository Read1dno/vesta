#include <stdafx.hpp>

namespace platform::windows {

	bool module_catalog::discover( const process_session& process )
	{
		const auto lookup = [ &process ]( std::string_view name )
		{
			return process.module_base( name );
		};

		client = lookup( "client.dll" );
		engine = lookup( "engine2.dll" );
		input_system = lookup( "inputsystem.dll" );
		tier = lookup( "tier0.dll" );
		schema = lookup( "schemasystem.dll" );
		physics = lookup( "vphysics2.dll" );
		panorama = lookup( "panorama.dll" );

		// Panorama is optional during the earliest CS2 startup phase. Features that
		// need it resolve it lazily once the UI module has actually loaded.
		return client && engine && input_system && tier && schema && physics;
	}

} // namespace platform::windows
