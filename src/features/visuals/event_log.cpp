#include <stdafx.hpp>
#include <features/visuals/event_log.hpp>
#include <config/settings.hpp>

namespace features::visuals {

	void event_log_t::push( std::string text, const event_kind kind )
	{
		if ( !config::general_settings.m_event_log.enabled ) return;
		if ( text.empty( ) ) return;
		std::scoped_lock lock( this->m_mutex );
		this->m_entries.push_back( {
			++this->m_sequence, std::move( text ), kind,
			std::chrono::steady_clock::now( ) } );
		while ( this->m_entries.size( ) > 64 ) this->m_entries.pop_front( );
	}

	void event_log_t::push_throttled( const std::uint32_t key,
		const std::string_view text, const event_kind kind,
		const std::chrono::milliseconds interval )
	{
		if ( !config::general_settings.m_event_log.enabled ) return;
		const auto now = std::chrono::steady_clock::now( );
		std::scoped_lock lock( this->m_mutex );
		for ( auto& [ stored_key, timestamp ] : this->m_throttles )
		{
			if ( stored_key != key ) continue;
			if ( now - timestamp < interval ) return;
			timestamp = now;
			this->m_entries.push_back( {
				++this->m_sequence, std::string( text ), kind, now } );
			while ( this->m_entries.size( ) > 64 ) this->m_entries.pop_front( );
			return;
		}
		this->m_throttles.emplace_back( key, now );
		this->m_entries.push_back( {
			++this->m_sequence, std::string( text ), kind, now } );
		while ( this->m_entries.size( ) > 64 ) this->m_entries.pop_front( );
	}

	std::vector<event_log_entry> event_log_t::snapshot(
		const float lifetime_seconds, const int maximum ) const
	{
		const auto now = std::chrono::steady_clock::now( );
		const auto lifetime = std::chrono::duration<float>(
			std::clamp( lifetime_seconds, 0.5f, 20.0f ) );
		const auto limit = std::clamp( maximum, 1, 12 );
		std::vector<event_log_entry> result{};
		result.reserve( static_cast<std::size_t>( limit ) );
		std::scoped_lock lock( this->m_mutex );
		for ( auto it = this->m_entries.rbegin( ); it != this->m_entries.rend( )
			&& static_cast<int>( result.size( ) ) < limit; ++it )
		{
			if ( now - it->timestamp <= lifetime ) result.push_back( *it );
		}
		std::ranges::reverse( result );
		return result;
	}

}
