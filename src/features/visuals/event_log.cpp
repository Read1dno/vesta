#include <stdafx.hpp>
#include <features/visuals/event_log.hpp>
#include <config/settings.hpp>

namespace features::visuals {

	void event_log_t::push( std::string text, const event_kind kind,
		const event_category category )
	{
		if ( !config::general_settings.m_event_log.enabled ) return;
		if ( text.empty( ) ) return;
		std::scoped_lock lock( this->m_mutex );
		this->m_entries.push_back( {
			++this->m_sequence, std::move( text ), kind, category,
			std::chrono::steady_clock::now( ) } );
		while ( this->m_entries.size( ) > 64 ) this->m_entries.pop_front( );
	}

	void event_log_t::push_throttled( const std::uint32_t key,
		const std::string_view text, const event_kind kind,
		const std::chrono::milliseconds interval, const event_category category )
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
				++this->m_sequence, std::string( text ), kind, category, now } );
			while ( this->m_entries.size( ) > 64 ) this->m_entries.pop_front( );
			return;
		}
		this->m_throttles.emplace_back( key, now );
		this->m_entries.push_back( {
			++this->m_sequence, std::string( text ), kind, category, now } );
		while ( this->m_entries.size( ) > 64 ) this->m_entries.pop_front( );
	}

	std::uint64_t event_log_t::begin_trigger_shot( std::string source,
		const bool seed )
	{
		if ( !config::general_settings.m_event_log.enabled || source.empty( ) ) return 0;
		const auto now = std::chrono::steady_clock::now( );
		std::scoped_lock lock( this->m_mutex );
		const auto sequence = ++this->m_sequence;
		this->m_entries.push_back( { sequence,
			std::format( "{}: shot pending", source ), event_kind::info,
			event_category::shot, now, now + std::chrono::milliseconds( 1200 ),
			std::move( source ), true, false, seed } );
		while ( this->m_entries.size( ) > 64 ) this->m_entries.pop_front( );
		return sequence;
	}

	void event_log_t::mark_latest_trigger_consumed( )
	{
		if ( !config::general_settings.m_event_log.enabled ) return;
		const auto now = std::chrono::steady_clock::now( );
		std::scoped_lock lock( this->m_mutex );
		for ( auto it = this->m_entries.rbegin( ); it != this->m_entries.rend( ); ++it )
		{
			if ( !it->pending || it->category != event_category::shot ) continue;
			if ( now > it->deadline ) break;
			it->command_consumed = true;
			it->text = std::format( "{}: shot registered", it->source );
			return;
		}
	}

	bool event_log_t::resolve_latest_trigger_shot( const int damage,
		const bool killed )
	{
		if ( !config::general_settings.m_event_log.enabled || damage <= 0 ) return false;
		const auto now = std::chrono::steady_clock::now( );
		std::scoped_lock lock( this->m_mutex );
		for ( auto it = this->m_entries.rbegin( ); it != this->m_entries.rend( ); ++it )
		{
			if ( !it->pending || it->category != event_category::shot ) continue;
			if ( now > it->deadline ) break;
			it->pending = false;
			it->command_consumed = true;
			it->kind = killed ? event_kind::kill : event_kind::hit;
			it->category = killed ? event_category::kill : event_category::hit;
			it->timestamp = now;
			it->text = std::format( "{}: {} - {} damage", it->source,
				killed ? "kill" : "hit", damage );
			return true;
		}
		return false;
	}

	std::vector<event_log_entry> event_log_t::snapshot(
		const float lifetime_seconds, const int maximum )
	{
		const auto now = std::chrono::steady_clock::now( );
		const auto lifetime = std::chrono::duration<float>(
			std::clamp( lifetime_seconds, 0.5f, 20.0f ) );
		const auto limit = std::clamp( maximum, 1, 12 );
		const auto& cfg = config::general_settings.m_event_log;
		const auto visible = [ & ]( const event_category category )
		{
			switch ( category )
			{
			case event_category::shot: return cfg.show_shots;
			case event_category::hit: return cfg.show_hits;
			case event_category::kill: return cfg.show_kills;
			case event_category::miss: return cfg.show_misses;
			case event_category::blocked: return cfg.show_blocked;
			default: return cfg.show_info;
			}
		};
		std::vector<event_log_entry> result{};
		result.reserve( static_cast<std::size_t>( limit ) );
		std::scoped_lock lock( this->m_mutex );
		for ( auto& entry : this->m_entries )
		{
			if ( !entry.pending || now < entry.deadline ) continue;
			entry.pending = false;
			entry.kind = event_kind::blocked;
			entry.category = event_category::miss;
			entry.timestamp = now;
			if ( !entry.command_consumed )
				entry.text = std::format( "{}: shot rejected - input not consumed",
					entry.source );
			else if ( entry.seed )
				entry.text = std::format( "{}: miss - no server damage (possible tick/seed mismatch)",
					entry.source );
			else
				entry.text = std::format( "{}: miss - no server-confirmed damage",
					entry.source );
		}
		for ( auto it = this->m_entries.rbegin( ); it != this->m_entries.rend( )
			&& static_cast<int>( result.size( ) ) < limit; ++it )
		{
			if ( now - it->timestamp <= lifetime && visible( it->category ) )
				result.push_back( *it );
		}
		std::ranges::reverse( result );
		return result;
	}

}
