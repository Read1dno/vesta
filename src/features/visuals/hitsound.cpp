#include <stdafx.hpp>
#include <features/visuals/hitsound.hpp>
#include <resources/sounds/hitsounds.hpp>

#include <xaudio2.h>
#include <condition_variable>
#include <deque>

namespace features::visuals {

namespace {

struct wav_view
{
	WAVEFORMATEX format{};
	const BYTE* samples{};
	UINT32 sample_bytes{};
};

[[nodiscard]] std::uint16_t read_u16( const unsigned char* value )
{
	return static_cast<std::uint16_t>( value[ 0 ] | value[ 1 ] << 8 );
}

[[nodiscard]] std::uint32_t read_u32( const unsigned char* value )
{
	return static_cast<std::uint32_t>( value[ 0 ] ) |
		static_cast<std::uint32_t>( value[ 1 ] ) << 8 |
		static_cast<std::uint32_t>( value[ 2 ] ) << 16 |
		static_cast<std::uint32_t>( value[ 3 ] ) << 24;
}

[[nodiscard]] std::optional<wav_view> parse_wav( const unsigned char* bytes, std::size_t size )
{
	if ( !bytes || size < 44 || std::memcmp( bytes, "RIFF", 4 ) != 0 ||
		std::memcmp( bytes + 8, "WAVE", 4 ) != 0 )
	{
		return std::nullopt;
	}

	wav_view result{};
	bool have_format{};
	for ( std::size_t cursor = 12; cursor + 8 <= size; )
	{
		const auto chunk_size = static_cast<std::size_t>( read_u32( bytes + cursor + 4 ) );
		const auto data = cursor + 8;
		if ( data + chunk_size > size ) break;
		if ( std::memcmp( bytes + cursor, "fmt ", 4 ) == 0 && chunk_size >= 16 )
		{
			result.format.wFormatTag = read_u16( bytes + data );
			result.format.nChannels = read_u16( bytes + data + 2 );
			result.format.nSamplesPerSec = read_u32( bytes + data + 4 );
			result.format.nAvgBytesPerSec = read_u32( bytes + data + 8 );
			result.format.nBlockAlign = read_u16( bytes + data + 12 );
			result.format.wBitsPerSample = read_u16( bytes + data + 14 );
			result.format.cbSize = 0;
			have_format = result.format.wFormatTag == WAVE_FORMAT_PCM;
		}
		else if ( std::memcmp( bytes + cursor, "data", 4 ) == 0 )
		{
			result.samples = reinterpret_cast<const BYTE*>( bytes + data );
			result.sample_bytes = static_cast<UINT32>( chunk_size );
		}
		cursor = data + chunk_size + ( chunk_size & 1u );
	}

	if ( !have_format || !result.samples || !result.sample_bytes ||
		!result.format.nChannels || !result.format.nSamplesPerSec )
	{
		return std::nullopt;
	}
	return result;
}

struct embedded_sound
{
	const unsigned char* bytes{};
	std::size_t size{};
};

constexpr std::array sounds{
	embedded_sound{ resources::sounds::soft, resources::sounds::soft_size },
	embedded_sound{ resources::sounds::glass, resources::sounds::glass_size },
	embedded_sound{ resources::sounds::pluck, resources::sounds::pluck_size },
	embedded_sound{ resources::sounds::crisp, resources::sounds::crisp_size },
	embedded_sound{ resources::sounds::flesh, resources::sounds::flesh_size }
};

}

struct hitsound_player::implementation
{
	struct request
	{
		int style{};
		float volume{};
	};
	struct voice
	{
		IXAudio2SourceVoice* source{};
	};

	std::mutex mutex{};
	std::condition_variable wake{};
	std::deque<request> requests{};
	bool stopping{};
	IXAudio2* engine{};
	IXAudio2MasteringVoice* mastering{};
	std::vector<voice> voices{};
	std::thread worker{};

	implementation( )
	{
		worker = std::thread( [ this ] { this->run( ); } );
	}

	~implementation( )
	{
		{
			std::scoped_lock lock( mutex );
			stopping = true;
		}
		wake.notify_one( );
		if ( worker.joinable( ) ) worker.join( );
		for ( auto& voice : voices )
		{
			if ( voice.source ) voice.source->DestroyVoice( );
		}
		if ( mastering ) mastering->DestroyVoice( );
		if ( engine ) engine->Release( );
	}

	void enqueue( int style, float volume )
	{
		{
			std::scoped_lock lock( mutex );
			if ( stopping ) return;
			if ( requests.size( ) >= 32 ) requests.pop_front( );
			requests.push_back( { style, volume } );
		}
		wake.notify_one( );
	}

	void run( )
	{
		for ( ;; )
		{
			request next{};
			{
				std::unique_lock lock( mutex );
				wake.wait( lock, [ & ] { return stopping || !requests.empty( ); } );
				if ( stopping && requests.empty( ) ) break;
				next = requests.front( );
				requests.pop_front( );
			}
			play_now( next.style, next.volume );
		}
	}

	bool initialize( )
	{
		if ( engine ) return mastering != nullptr;
		if ( FAILED( XAudio2Create( &engine, 0, XAUDIO2_DEFAULT_PROCESSOR ) ) ) return false;
		if ( FAILED( engine->CreateMasteringVoice( &mastering ) ) )
		{
			engine->Release( );
			engine = nullptr;
			return false;
		}
		voices.reserve( 16 );
		return true;
	}

	void collect_finished( )
	{
		std::erase_if( voices, [ ]( voice& value )
		{
			XAUDIO2_VOICE_STATE state{};
			value.source->GetState( &state, XAUDIO2_VOICE_NOSAMPLESPLAYED );
			if ( state.BuffersQueued ) return false;
			value.source->DestroyVoice( );
			return true;
		} );
	}

	void play_now( int style, float volume )
	{
		const auto index = static_cast<std::size_t>( std::clamp( style, 0,
			static_cast<int>( sounds.size( ) ) - 1 ) );
		const auto wave = parse_wav( sounds[ index ].bytes, sounds[ index ].size );
		if ( !wave || !initialize( ) ) return;
		collect_finished( );
		if ( voices.size( ) >= 16 )
		{
			auto& oldest = voices.front( );
			oldest.source->Stop( 0 );
			oldest.source->DestroyVoice( );
			voices.erase( voices.begin( ) );
		}

		IXAudio2SourceVoice* source{};
		if ( FAILED( engine->CreateSourceVoice( &source, &wave->format ) ) ) return;
		source->SetVolume( std::clamp( volume, 0.0f, 1.0f ) );
		XAUDIO2_BUFFER buffer{};
		buffer.Flags = XAUDIO2_END_OF_STREAM;
		buffer.AudioBytes = wave->sample_bytes;
		buffer.pAudioData = wave->samples;
		if ( FAILED( source->SubmitSourceBuffer( &buffer ) ) || FAILED( source->Start( 0 ) ) )
		{
			source->DestroyVoice( );
			return;
		}
		voices.push_back( { source } );
	}
};

hitsound_player::hitsound_player( ) : m_impl( std::make_unique<implementation>( ) ) {}
hitsound_player::~hitsound_player( ) = default;

void hitsound_player::play( int style, float volume )
{
	if ( !this->m_impl || volume <= 0.0f ) return;
	this->m_impl->enqueue( style, std::clamp( volume, 0.0f, 1.0f ) );
}

}
